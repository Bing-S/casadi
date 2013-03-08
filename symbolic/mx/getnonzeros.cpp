/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "getnonzeros.hpp"
#include "mapping.hpp"
#include "../stl_vector_tools.hpp"
#include "../matrix/matrix_tools.hpp"
#include "mx_tools.hpp"
#include "../sx/sx_tools.hpp"
#include "../fx/sx_function.hpp"
#include "../matrix/sparsity_tools.hpp"

using namespace std;

namespace CasADi{

  GetNonzeros::GetNonzeros(const CRSSparsity& sp){
    setSparsity(sp);
    assigns_.resize(sp.size());
  }

  GetNonzeros* GetNonzeros::clone() const{
    return new GetNonzeros(*this);
  }

  void GetNonzeros::evaluateD(const DMatrixPtrV& input, DMatrixPtrV& output, const DMatrixPtrVV& fwdSeed, DMatrixPtrVV& fwdSens, const DMatrixPtrVV& adjSeed, DMatrixPtrVV& adjSens){
    evaluateGen<double,DMatrixPtrV,DMatrixPtrVV>(input,output,fwdSeed,fwdSens,adjSeed,adjSens);
  }

  void GetNonzeros::evaluateSX(const SXMatrixPtrV& input, SXMatrixPtrV& output, const SXMatrixPtrVV& fwdSeed, SXMatrixPtrVV& fwdSens, const SXMatrixPtrVV& adjSeed, SXMatrixPtrVV& adjSens){
    evaluateGen<SX,SXMatrixPtrV,SXMatrixPtrVV>(input,output,fwdSeed,fwdSens,adjSeed,adjSens);
  }

  template<typename T, typename MatV, typename MatVV>
  void GetNonzeros::evaluateGen(const MatV& input, MatV& output, const MatVV& fwdSeed, MatVV& fwdSens, const MatVV& adjSeed, MatVV& adjSens){
    casadi_assert(input.size()==1);

    // Number of sensitivities
    int nadj = adjSeed.size();
    int nfwd = fwdSens.size();
    
    // Nondifferentiated outputs
    if(input[0]!=0 && output[0]!=0){
      for(int k=0; k<assigns_.size(); ++k){
	output[0]->data()[k] = input[0]->data()[assigns_[k]];
      }
    }
    
    // Forward sensitivities
    for(int d=0; d<nfwd; ++d){
      if(fwdSeed[d][0]!=0 && fwdSens[d][0]!=0){
	for(int k=0; k<assigns_.size(); ++k){
	  fwdSens[d][0]->data()[k] = fwdSeed[d][0]->data()[assigns_[k]];
	}
      }
    }
      
    // Adjoint sensitivities
    for(int d=0; d<nadj; ++d){
      if(adjSeed[d][0]!=0 && adjSens[d][0]!=0){
	for(int k=0; k<assigns_.size(); ++k){
	  adjSens[d][0]->data()[assigns_[k]] += adjSeed[d][0]->data()[k];
	  adjSeed[d][0]->data()[k] = 0;
	}
      }
    }
  }

  void GetNonzeros::propagateSparsity(DMatrixPtrV& input, DMatrixPtrV& output, bool fwd){
    casadi_assert(input.size()==1);
      
    // Nondifferentiated outputs
    if(input[0]!=0 && output[0]!=0){
      
      // Get references to the assignment operations and data
      bvec_t *outputd = get_bvec_t(output[0]->data());
      bvec_t *inputd = get_bvec_t(input[0]->data());
      
      // Propate sparsity
      for(int k=0; k<assigns_.size(); ++k){
	if(fwd){
	  outputd[k] = inputd[assigns_[k]];
	} else {
	  inputd[assigns_[k]] |= outputd[k];
	  outputd[k] = 0;
	}
      }
    }
  }

  void GetNonzeros::printPart(std::ostream &stream, int part) const{
    if(ndep()==0){
      stream << "sparse(" << size1() << "," << size2() << ")";
    } else if(numel()==1 && size()==1 && ndep()==1){
      if(part==1)
	if(dep(0).numel()>1)
	  stream << "[" << assigns_[0] << "]";
    } else {
      if(part==0){
	stream << "mapping(";
	if(sparsity().dense())            stream << "dense";
	else if(sparsity().diagonal())    stream << "diagonal";
	else                              stream << "sparse";
	stream << " " << size1() << "-by-" << size2() << " matrix, dependencies: [";
      } else if(part==ndep()){
	stream << "], nonzeros: [";
	for(int k=0; k<assigns_.size(); ++k){
	  stream << assigns_[k] << ",";
	}
	stream << "])";      
      } else {
	stream << ",";
      }
    }
  }

  void GetNonzeros::assign(const MX& d, const std::vector<int>& inz, bool add){
    // Quick return if no elements
    if(inz.empty()) return;
  
    casadi_assert(!d.isNull());
    casadi_assert(inz.size()==size());
  
    // Add the node if it is not already a dependency
    int depind = addDependency(d);
    
    // Save the mapping
    for(int k=0; k<inz.size(); ++k){
      assigns_[k] = inz[k];
    }
  }

  void GetNonzeros::init(){
    // Call init of the base class
    MXNode::init();

    casadi_assert(ndep()==1);
  
    // Clear the runtime
    assigns2_.clear();
  
    // For all the output nonzeros
    for(int onz=0; onz<assigns_.size(); ++onz){
      assigns2_.push_back(pair<int,int>(assigns_[onz],onz));
    }
  }

  void GetNonzeros::evaluateMX(const MXPtrV& input, MXPtrV& output, const MXPtrVV& fwdSeed, MXPtrVV& fwdSens, const MXPtrVV& adjSeed, MXPtrVV& adjSens, bool output_given){
    casadi_assert(input.size()==1);

    // Sparsity
    const CRSSparsity &sp = sparsity();
    vector<int> row = sp.getRow();
  
    // Number of derivative directions
    int nfwd = fwdSens.size();
    int nadj = adjSeed.size();

    // Function evaluation in sparse triplet format
    vector<int> r_row, r_col, r_inz;
  
    // Sensitivity matrices in sparse triplet format for all forward sensitivities
    vector<vector<int> > f_row(nfwd), f_col(nfwd), f_inz(nfwd);
  
    // Sensitivity matrices in sparse triplet format for all adjoint sensitivities
    vector<vector<int> > a_row(nadj), a_col(nadj), a_onz(nadj);
    
    // For all outputs
    if(output[0]!=0){
    
      // Output sparsity
      const CRSSparsity &osp = sparsity();
      const vector<int>& ocol = osp.col();
      vector<int> orow = osp.getRow();
    
      // Skip if input doesn't exist
      if(input[0]!=0){
      
	// Input sparsity
	const CRSSparsity &isp = dep().sparsity();
	const vector<int>& icol = isp.col();
	vector<int> irow = isp.getRow();

	// Find out which matrix elements that we are trying to calculate
	vector<int> el_wanted(assigns2_.size()); // TODO: Move outside loop
	for(int k=0; k<assigns2_.size(); ++k){
	  el_wanted[k] = orow[assigns2_[k].second] + ocol[assigns2_[k].second]*osp.size1();
	}

	// We next need to resort the assigns vector with increasing inputs instead of outputs
	// Start by counting the number of inputs corresponding to each nonzero
	vector<int> inz_count(icol.size()+1,0);
	for(vector<pair<int,int> >::const_iterator it=assigns2_.begin(); it!=assigns2_.end(); ++it){
	  inz_count[it->first+1]++;
	}
      
	// Cumsum to get index offset for input nonzero
	for(int i=0; i<icol.size(); ++i){
	  inz_count[i+1] += inz_count[i];
	}
      
	// Get the order of assignments
	vector<int> assigns2_order(assigns2_.size()); // TODO: Move allocation outside loop
	for(int k=0; k<assigns2_.size(); ++k){
	  // Save the new index
	  assigns2_order[inz_count[assigns2_[k].first]++] = k;
	}

	// Find out which matrix elements that we are trying to calculate
	vector<int> el_known(assigns2_.size()); // TODO: Move allocation outside loop
	for(int k=0; k<assigns2_.size(); ++k){
	  // Get output nonzero
	  int inz_k = assigns2_[assigns2_order[k]].first;
        
	  // Get element
	  el_known[k] = irow[inz_k] + icol[inz_k]*isp.size1();
	}

	// Temporary vectors
	vector<int> temp = el_known;
      
	// Evaluate the nondifferentiated function
	if(!output_given){
        
	  // Get the matching nonzeros
	  input[0]->sparsity().getNZInplace(temp);

	  // Add to sparsity pattern
	  for(int k=0; k<assigns2_.size(); ++k){
	    if(temp[k]!=-1){
	      r_inz.push_back(temp[k]);
	      r_col.push_back(ocol[assigns2_[assigns2_order[k]].second]);
	      r_row.push_back(orow[assigns2_[assigns2_order[k]].second]);
	    }
	  }
	}
      
	// Forward sensitivities
	for(int d=0; d<nfwd; ++d){
        
	  // Get the matching nonzeros
	  copy(el_known.begin(),el_known.end(),temp.begin());
	  fwdSeed[d][0]->sparsity().getNZInplace(temp);

	  // Add to sparsity pattern
	  for(int k=0; k<assigns2_.size(); ++k){
	    if(temp[k]!=-1){
	      f_inz[d].push_back(temp[k]);
	      f_col[d].push_back(ocol[assigns2_[assigns2_order[k]].second]);
	      f_row[d].push_back(orow[assigns2_[assigns2_order[k]].second]);
	    }
	  }
	}
      
	// Continue of no adjoint sensitivities
	if(nadj>0){
      
	  // Resize temp to be able to hold el_wanted
	  temp.resize(el_wanted.size());
	  
	  // Adjoint sensitivities
	  for(int d=0; d<nadj; ++d){
	    
	    // Get the matching nonzeros
	    copy(el_wanted.begin(),el_wanted.end(),temp.begin());
	    adjSeed[d][0]->sparsity().getNZInplace(temp);
	    
	    // Add to sparsity pattern
	    for(int k=0; k<assigns2_.size(); ++k){
	      if(temp[k]!=-1){
		a_onz[d].push_back(temp[k]);
		a_col[d].push_back(icol[assigns2_[k].first]);
		a_row[d].push_back(irow[assigns2_[k].first]);
	      }
	    }
	  }
	}
      }
    }
    
    // Non-differentiated output
    if(!output_given){
      // For all outputs
      if(output[0]!=0){

	// Output sparsity
	const CRSSparsity &osp = sparsity();

	// Create a sparsity pattern from vectors
	CRSSparsity r_sp = sp_triplet(osp.size1(),osp.size2(),r_row,r_col);
      
	if(input[0]==0){
	  *output[0] = MX::zeros(r_sp);
	} else {
	  if(r_inz.size()>0){
	    *output[0] = MX::create(new GetNonzeros(r_sp));
	    (*output[0])->assign(*input[0],r_inz);
	  } else {
	    *output[0] = MX::zeros(r_sp);
	  }
	}
      }
    }
    
    // Create the forward sensitivity matrices
    for(int d=0; d<nfwd; ++d){
    
      if(output[0]!=0){

	// Output sparsity
	const CRSSparsity &osp = sparsity();

	// Create a sparsity pattern from vectors
	CRSSparsity f_sp = sp_triplet(osp.size1(),osp.size2(),f_row[d],f_col[d]);
      
	if(input[0]==0){
	  *fwdSens[d][0] = MX::zeros(f_sp);
	} else {
	  if(f_inz[d].size()>0){
	    *fwdSens[d][0] = MX::create(new GetNonzeros(f_sp));
	    (*fwdSens[d][0])->assign(*fwdSeed[d][0],f_inz[d]);
	  } else {
	    *fwdSens[d][0] = MX::zeros(f_sp);
	  }
	}
      }
    }
  
    // Create the adjoint sensitivity matrices
    vector<int> a_inz;
    for(int d=0; d<nadj; ++d){

      // Skip if input doesn't exist
      if(input[0]!=0){
        
	// Input sparsity
	const CRSSparsity &isp = input[0]->sparsity();
      
	// Create a sparsity pattern from vectors
	CRSSparsity a_sp = sp_triplet(isp.size1(),isp.size2(),a_row[d],a_col[d],a_inz,true);
      
	// Create a mapping matrix
	MX s = MX::create(new Mapping(a_sp));
     
	// Add dependencies
	if(output[0]!=0){
	  s->assign(*adjSeed[d][0],a_onz[d],a_onz[d],true);
	}
      
	// Save to adjoint sensitivities
	*adjSens[d][0] += s;
      }
    
      // Clear adjoint seeds
      if(adjSeed[d][0] != 0){
	*adjSeed[d][0] = MX();
      }
    }
  }
  
  Matrix<int> GetNonzeros::mapping(int iind) const {
    casadi_assert(ndep()==1);
    casadi_assert(iind==0);

    // TODO: make this efficient
    std::vector< int > row;
    std::vector< int > col;
    sparsity().getSparsity(row,col);
    Matrix<int> ret(size1(),size2());
    for (int k=0;k<assigns_.size();++k) { // Loop over output non-zeros
      int el = assigns_[k];
      ret(row[k],col[k]) = el;
    }
    return ret;
  }

  std::vector<int> GetNonzeros::getDepInd() const {
    std::vector<int> ret(size(),0);
    return ret;
  }

  bool GetNonzeros::isIdentity() const{
    // Check sparsity
    if(!(sparsity() == dep().sparsity()))
      return false;
      
    // Check if the nonzeros follow in increasing order
    for(int k=0; k<assigns_.size(); ++k){
      if(assigns_[k] != k) return false;
    }
    
    // True if reached this point
    return true;
  }

  void GetNonzeros::generateOperation(std::ostream &stream, const std::vector<std::string>& arg, const std::vector<std::string>& res, CodeGenerator& gen) const{
    // Clear the result
    stream << "  for(i=0; i<" << sparsity().size() << "; ++i) " << res.front() << "[i]=0;" << endl;

    vector<int> tmp1,tmp2;
    tmp1.resize(assigns2_.size());
    tmp2.resize(assigns2_.size());
    for(int i=0; i<assigns2_.size(); ++i){
      tmp1[i] = assigns2_[i].first;
      tmp2[i] = assigns2_[i].second;
    }
    // Condegen the indices
    int ind1 = gen.getConstant(tmp1,true);
    int ind2 = gen.getConstant(tmp2,true);
    
    // Codegen the assignments
    stream << "  for(i=0; i<" << assigns2_.size() << "; ++i) " << res.front() << "[s" << ind2 << "[i]] += " << arg.front() << "[s" << ind1 << "[i]];" << endl;
  }

  void GetNonzeros::simplifyMe(MX& ex){
    // Simplify if identity
    if(isIdentity()){
      MX t = dep(0);
      ex = t;
    }
  }


} // namespace CasADi
