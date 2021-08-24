/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
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


#include "cvodes_simulator.hpp"
#include "casadi/core/casadi_misc.hpp"

#define THROWING(fcn, ...) \
cvodes_error(CASADI_STR(fcn), fcn(__VA_ARGS__))

using namespace std;
namespace casadi {

  extern "C"
  int CASADI_SIMULATOR_CVODES_EXPORT
  casadi_register_simulator_cvodes(Simulator::Plugin* plugin) {
    plugin->creator = CvodesSimulator::creator;
    plugin->name = "cvodes";
    plugin->doc = CvodesSimulator::meta_doc.c_str();;
    plugin->version = CASADI_VERSION;
    plugin->options = &CvodesSimulator::options_;
    return 0;
  }

  extern "C"
  void CASADI_SIMULATOR_CVODES_EXPORT casadi_load_simulator_cvodes() {
    Simulator::registerPlugin(casadi_register_simulator_cvodes);
  }

  CvodesSimulator::CvodesSimulator(const std::string& name, const Function& dae,
    const std::vector<double>& grid)
    : SundialsSimulator(name, dae, grid) {
  }

  CvodesSimulator::~CvodesSimulator() {
    clear_mem();
  }

  const Options CvodesSimulator::options_
  = {{&SundialsSimulator::options_},
     {{"linear_multistep_method",
       {OT_STRING,
        "Simulator scheme: BDF|adams"}},
      {"nonlinear_solver_iteration",
       {OT_STRING,
        "Nonlinear solver type: NEWTON|functional"}},
      {"min_step_size",
       {OT_DOUBLE,
        "Min step size [default: 0/0.0]"}},
      {"fsens_all_at_once",
       {OT_BOOL,
        "Calculate all right hand sides of the sensitivity equations at once"}}
     }
  };

  void CvodesSimulator::init(const Dict& opts) {
    if (verbose_) casadi_message(name_ + "::init");

    // Initialize the base classes
    SundialsSimulator::init(opts);

    // Default options
    string linear_multistep_method = "bdf";
    string nonlinear_solver_iteration = "newton";
    min_step_size_ = 0;

    // Read options
    for (auto&& op : opts) {
      if (op.first=="linear_multistep_method") {
        linear_multistep_method = op.second.to_string();
      } else if (op.first=="min_step_size") {
        min_step_size_ = op.second;
      } else if (op.first=="nonlinear_solver_iteration") {
        nonlinear_solver_iteration = op.second.to_string();
      }
    }

    // Create function
    create_function("odeF", {"x", "p", "t"}, {"ode"});

    // Algebraic variables not supported
    casadi_assert(nz_==0, "CVODES does not support algebraic variables");

    if (linear_multistep_method=="adams") {
      lmm_ = CV_ADAMS;
    } else if (linear_multistep_method=="bdf") {
      lmm_ = CV_BDF;
    } else {
      casadi_error("Unknown linear multistep method: " + linear_multistep_method);
    }

    if (nonlinear_solver_iteration=="newton") {
      iter_ = CV_NEWTON;
    } else if (nonlinear_solver_iteration=="functional") {
      iter_ = CV_FUNCTIONAL;
    } else {
      casadi_error("Unknown nonlinear solver iteration: " + nonlinear_solver_iteration);
    }

    // Attach functions for jacobian information
    if (newton_scheme_!=SD_DIRECT || (ns_>0 && second_order_correction_)) {
      create_function("jtimesF", {"t", "x", "p", "fwd:x"}, {"fwd:ode"});
    }
  }

  int CvodesSimulator::init_mem(void* mem) const {
    if (SundialsSimulator::init_mem(mem)) return 1;
    auto m = to_mem(mem);

    // Create CVodes memory block
    m->mem = CVodeCreate(lmm_, iter_);
    casadi_assert(m->mem!=nullptr, "CVodeCreate: Creation failed");

    // Set error handler function
    THROWING(CVodeSetErrHandlerFn, m->mem, ehfun, m);

    // Set user data
    THROWING(CVodeSetUserData, m->mem, m);

    // Initialize CVodes
    double t0 = 0;
    THROWING(CVodeInit, m->mem, rhs, t0, m->xz);

    // Set tolerances
    THROWING(CVodeSStolerances, m->mem, reltol_, abstol_);

    // Maximum number of steps
    THROWING(CVodeSetMaxNumSteps, m->mem, max_num_steps_);

    // Initial step size
    if (step0_!=0) THROWING(CVodeSetInitStep, m->mem, step0_);

    // Min step size
    if (min_step_size_!=0) THROWING(CVodeSetMinStep, m->mem, min_step_size_);

    // Max step size
    if (max_step_size_!=0) THROWING(CVodeSetMaxStep, m->mem, max_step_size_);

    // Maximum order of method
    if (max_order_) THROWING(CVodeSetMaxOrd, m->mem, max_order_);

    // Coeff. in the nonlinear convergence test
    if (nonlin_conv_coeff_!=0) THROWING(CVodeSetNonlinConvCoef, m->mem, nonlin_conv_coeff_);

    // attach a linear solver
    if (newton_scheme_==SD_DIRECT) {
      // Direct scheme
      CVodeMem cv_mem = static_cast<CVodeMem>(m->mem);
      cv_mem->cv_lmem   = m;
      cv_mem->cv_lsetup = lsetup;
      cv_mem->cv_lsolve = lsolve;
      cv_mem->cv_setupNonNull = TRUE;
    } else {
      // Iterative scheme
      casadi_int pretype = use_precon_ ? PREC_LEFT : PREC_NONE;
      switch (newton_scheme_) {
      case SD_DIRECT: casadi_assert_dev(0);
      case SD_GMRES: THROWING(CVSpgmr, m->mem, pretype, max_krylov_); break;
      case SD_BCGSTAB: THROWING(CVSpbcg, m->mem, pretype, max_krylov_); break;
      case SD_TFQMR: THROWING(CVSptfqmr, m->mem, pretype, max_krylov_); break;
      }
      THROWING(CVSpilsSetJacTimesVecFn, m->mem, jtimes);
      if (use_precon_) THROWING(CVSpilsSetPreconditioner, m->mem, psetup, psolve);
    }
    return 0;
  }

  int CvodesSimulator::rhs(double t, N_Vector x, N_Vector xdot, void *user_data) {
    try {
      casadi_assert_dev(user_data);
      auto m = to_mem(user_data);
      auto& s = m->self;
      m->arg[0] = NV_DATA_S(x);
      m->arg[1] = m->p;
      m->arg[2] = &t;
      m->res[0] = NV_DATA_S(xdot);
      s.calc_function(m, "odeF");
      return 0;
    } catch(int flag) { // recoverable error
      return flag;
    } catch(exception& e) { // non-recoverable error
      uerr() << "rhs failed: " << e.what() << endl;
      return -1;
    }
  }

  void CvodesSimulator::reset(SimulatorMemory* mem, double t, const double* x, const double* z,
      const double* p, double* y) const {
    if (verbose_) casadi_message(name_ + "::reset");
    auto m = to_mem(mem);
    // Reset the base classes
    SundialsSimulator::reset(mem, t, x, z, p, y);
    // Re-initialize
    THROWING(CVodeReInit, m->mem, t, m->xz);
    // Set the stop time of the integration -- don't integrate past this point
    if (stop_at_end_) setStopTime(m, grid_.back());
    // Get outputs
    if (y && ny_ > 0) eval_y(m, t, x, z, p, y);
  }

  void CvodesSimulator::advance(SimulatorMemory* mem, double t, double* x, double* z,
      double* y) const {
    auto m = to_mem(mem);

    casadi_assert(t >= grid_.front(),
      "CvodesSimulator::integrate(" + str(t) + "): "
      "Cannot integrate to a time earlier than t0 (" + str(grid_.front()) + ")");
    casadi_assert(t <= grid_.back() || !stop_at_end_,
      "CvodesSimulator::integrate(" + str(t) + "): "
      "Cannot integrate past a time later than tf (" + str(grid_.back()) + ") "
      "unless stop_at_end is set to False.");
    // Integrate, unless already at desired time
    const double ttol = 1e-9;
    if (fabs(m->t-t)>=ttol) {
      // Integrate forward ...
      THROWING(CVode, m->mem, t, m->xz, &m->t, CV_NORMAL);
    }
    // Set function outputs
    casadi_copy(NV_DATA_S(m->xz), nx_, x);
    // Get outputs
    if (y && ny_ > 0) eval_y(m, t, x, z, m->p, y);
    // Get stats
    THROWING(CVodeGetIntegratorStats, m->mem, &m->nsteps, &m->nfevals, &m->nlinsetups,
             &m->netfails, &m->qlast, &m->qcur, &m->hinused,
             &m->hlast, &m->hcur, &m->tcur);
    THROWING(CVodeGetNonlinSolvStats, m->mem, &m->nniters, &m->nncfails);
  }

  void CvodesSimulator::cvodes_error(const char* module, int flag) {
    // Successfull return or warning
    if (flag>=CV_SUCCESS) return;
    // Construct error message
    char* flagname = CVodeGetReturnFlagName(flag);
    stringstream ss;
    ss << module << " returned \"" << flagname << "\". Consult CVODES documentation.";
    free(flagname); // NOLINT
    casadi_error(ss.str());
  }

  void CvodesSimulator::ehfun(int error_code, const char *module, const char *function,
                              char *msg, void *user_data) {
    try {
      casadi_assert_dev(user_data);
      auto m = to_mem(user_data);
      auto& s = m->self;
      if (!s.disable_internal_warnings_) {
        uerr() << msg << endl;
      }
    } catch(exception& e) {
      uerr() << "ehfun failed: " << e.what() << endl;
    }
  }

  int CvodesSimulator::jtimes(N_Vector v, N_Vector Jv, double t, N_Vector x,
                              N_Vector xdot, void *user_data, N_Vector tmp) {
    try {
      auto m = to_mem(user_data);
      auto& s = m->self;
      m->arg[0] = &t;
      m->arg[1] = NV_DATA_S(x);
      m->arg[2] = m->p;
      m->arg[3] = NV_DATA_S(v);
      m->res[0] = NV_DATA_S(Jv);
      s.calc_function(m, "jtimesF");
      return 0;
    } catch(casadi_int flag) { // recoverable error
      return flag;
    } catch(exception& e) { // non-recoverable error
      uerr() << "jtimes failed: " << e.what() << endl;
      return -1;
    }
  }

  void CvodesSimulator::setStopTime(SimulatorMemory* mem, double tf) const {
    // Set the stop time of the integration -- don't integrate past this point
    auto m = to_mem(mem);
    THROWING(CVodeSetStopTime, m->mem, tf);
  }

  int CvodesSimulator::psolve(double t, N_Vector x, N_Vector xdot, N_Vector r,
                              N_Vector z, double gamma, double delta, int lr,
                              void *user_data, N_Vector tmp) {
    try {
      auto m = to_mem(user_data);
      auto& s = m->self;

      // Get right-hand sides in m->v1
      double* v = NV_DATA_S(r);
      casadi_copy(v, s.nx_, m->v1);

      // Solve for undifferentiated right-hand-side, save to output
      if (s.linsolF_.solve(m->jac, m->v1, 1, false, m->mem_linsolF))
        casadi_error("Linear system solve failed");
      v = NV_DATA_S(z); // possibly different from r
      casadi_copy(m->v1, s.nx1_, v);

      // Sensitivity equations
      if (s.ns_>0) {
        // Second order correction
        if (s.second_order_correction_) {
          // The outputs will double as seeds for jtimesF
          casadi_clear(v + s.nx1_, s.nx_ - s.nx1_);
          m->arg[0] = &t; // t
          m->arg[1] = NV_DATA_S(x); // x
          m->arg[2] = m->p; // p
          m->arg[3] = v; // fwd:x
          m->res[0] = m->v2; // fwd:ode
          s.calc_function(m, "jtimesF");

          // Subtract m->v2 from m->v1, scaled with -gamma
          casadi_axpy(s.nx_ - s.nx1_, m->gamma, m->v2 + s.nx1_, m->v1 + s.nx1_);
        }

        // Solve for sensitivity right-hand-sides
        if (s.linsolF_.solve(m->jac, m->v1 + s.nx1_, s.ns_, false, m->mem_linsolF))
          casadi_error("Linear solve failed");

        // Save to output, reordered
        casadi_copy(m->v1 + s.nx1_, s.nx_-s.nx1_, v+s.nx1_);
      }

      return 0;
    } catch(int flag) { // recoverable error
      return flag;
    } catch(exception& e) { // non-recoverable error
      uerr() << "psolve failed: " << e.what() << endl;
      return -1;
    }
  }

  int CvodesSimulator::psetup(double t, N_Vector x, N_Vector xdot, booleantype jok,
                              booleantype *jcurPtr, double gamma, void *user_data,
                              N_Vector tmp1, N_Vector tmp2, N_Vector tmp3) {
    try {
      auto m = to_mem(user_data);
      auto& s = m->self;
      // Store gamma for later
      m->gamma = gamma;

      // Calculate Jacobian
      double d1 = -gamma, d2 = 1.;
      m->arg[0] = &t;
      m->arg[1] = NV_DATA_S(x);
      m->arg[2] = m->p;
      m->arg[3] = &d1;
      m->arg[4] = &d2;
      m->res[0] = m->jac;
      if (s.calc_function(m, "jacF")) casadi_error("'jacF' calculation failed");

      // Prepare the solution of the linear system (e.g. factorize)
      if (s.linsolF_.nfact(m->jac, m->mem_linsolF)) casadi_error("'jacF' factorization failed");

      return 0;
    } catch(int flag) { // recoverable error
      return flag;
    } catch(exception& e) { // non-recoverable error
      uerr() << "psetup failed: " << e.what() << endl;
      return -1;
    }
  }

  int CvodesSimulator::lsetup(CVodeMem cv_mem, int convfail, N_Vector x, N_Vector xdot,
                              booleantype *jcurPtr,
                              N_Vector vtemp1, N_Vector vtemp2, N_Vector vtemp3) {
    try {
      auto m = to_mem(cv_mem->cv_lmem);
      //auto& s = m->self;

      // Current time
      double t = cv_mem->cv_tn;

      // Scaling factor before J
      double gamma = cv_mem->cv_gamma;

      // Call the preconditioner setup function (which sets up the linear solver)
      if (psetup(t, x, xdot, FALSE, jcurPtr,
                 gamma, static_cast<void*>(m), vtemp1, vtemp2, vtemp3)) return 1;

      return 0;
    } catch(int flag) { // recoverable error
      return flag;
    } catch(exception& e) { // non-recoverable error
      uerr() << "lsetup failed: " << e.what() << endl;
      return -1;
    }
  }

  int CvodesSimulator::lsolve(CVodeMem cv_mem, N_Vector b, N_Vector weight,
                              N_Vector x, N_Vector xdot) {
    try {
      auto m = to_mem(cv_mem->cv_lmem);
      //auto& s = m->self;

      // Current time
      double t = cv_mem->cv_tn;

      // Scaling factor before J
      double gamma = cv_mem->cv_gamma;

      // Accuracy
      double delta = 0.0;

      // Left/right preconditioner
      casadi_int lr = 1;

      // Call the preconditioner solve function (which solves the linear system)
      if (psolve(t, x, xdot, b, b, gamma, delta,
                 lr, static_cast<void*>(m), nullptr)) return 1;

      return 0;
    } catch(int flag) { // recoverable error
      return flag;
    } catch(exception& e) { // non-recoverable error
      uerr() << "lsolve failed: " << e.what() << endl;
      return -1;
    }
  }

  Function CvodesSimulator::getJ() const {
    return oracle_.is_a("SXFunction") ? getJ<SX>() : getJ<MX>();
  }

  template<typename MatType>
  Function CvodesSimulator::getJ() const {
    vector<MatType> a = MatType::get_input(oracle_);
    vector<MatType> r = const_cast<Function&>(oracle_)(a); // NOLINT
    MatType c_x = MatType::sym("c_x");
    MatType c_xdot = MatType::sym("c_xdot");

    // Get the Jacobian in the Newton iteration
    MatType jac = c_x*MatType::jacobian(r[DYN_ODE], a[DYN_X])
                + c_xdot*MatType::eye(nx_);
    return Function("jacF", {a[DYN_T], a[DYN_X], a[DYN_P], c_x, c_xdot}, {jac});
  }

  CvodesSimMemory::CvodesSimMemory(const CvodesSimulator& s) : self(s) {
    this->mem = nullptr;

    // Reset checkpoints counter
    this->ncheck = 0;
  }

  CvodesSimMemory::~CvodesSimMemory() {
    if (this->mem) CVodeFree(&this->mem);
  }

} // namespace casadi