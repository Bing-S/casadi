#! Monodromy matrix
#! =====================
from casadi import *
from numpy import *
from pylab import *

#! We will investigate the monodromy matrix with the help of a simple 2-state system, as found in 1. Nayfeh AH, Balachandran B. Applied nonlinear dynamics. 1995. Available at: http://onlinelibrary.wiley.com/doi/10.1002/9783527617548.biblio/summary [Accessed June 16, 2011], page 52.
#$ $\dot{x_1} = x_2$
#$ $\dot{x_2} = -(-w_0^2 x_1 + a_3 x_1^3 + a_5 x_1^5) - (2 mu_1 x_2 + mu_3 x_2^3) + f$.

t = SX("t")

x1,x2 = x  = ssym("x",2)
dx = ssym("dx",2)

w0 = SX("w0")
a3 = SX("a3")
a5 = SX("a5")
mu1 = SX("mu1")
mu3 = SX("mu3")
f = SX("f")

tf = 40

params = [w0,a3,a5,mu1,mu3,f]
rhs    = [x2,(-(-w0**2 *x1 + a3*x1**3 + a5*x1**5) - (2 *mu1 *x2 + mu3 * x2**3))/100]

f=SXFunction({'NUM': DAE_NUM_IN, DAE_T: t, DAE_Y: x, DAE_P: params, DAE_YDOT: dx},[rhs])
f.init()


integrator = CVodesIntegrator(f)
integrator.setOption("tf",tf)
integrator.setOption("reltol",1e-12)
integrator.setOption("abstol",1e-12)
integrator.init()

N = 500

#! Let's get acquainted with the system by drawing a phase portrait
ts = linspace(0,tf,N)

sim = Simulator(integrator,ts)
sim.init()

w0_ = 5.278
params_ = [ w0_, -1.402*w0_**2,  0.271*w0_**2,0,0,0 ]

sim.input(INTEGRATOR_P).set(params_)

x2_0 = 0
figure(1)
for x1_0 in [-3.5,-3.1,-3,-2,-1,0]:
  sim.input(INTEGRATOR_X0).set([x1_0,x2_0])
  sim.evaluate()
  plot(sim.output()[:,0],sim.output()[:,1],'k')

title('phase portrait for mu_1 = 0, mu_2 = 0')
xlabel('x_1')
ylabel('x_2')

show()

x0 = DMatrix([-3.1,0])

#! Monodromy matrix at tf - Jacobian of integrator
#! ===============================================
#! First argument is input index, second argument is output index
jac = Jacobian(integrator,INTEGRATOR_X0,INTEGRATOR_XF)
jac.init()

jac.input(INTEGRATOR_X0).set(x0)
jac.evaluate()

Ji = jac.output()

print Ji

#! Monodromy matrix at various instances - Jacobian of Simulator
#! =============================================================

jacsim = Jacobian(sim,INTEGRATOR_X0,0)
#! Only forward mode is supported for now

jacsim.setOption("ad_mode","forward")
jacsim.init()

jacsim.input(INTEGRATOR_X0).set(x0)
jacsim.input(INTEGRATOR_P).set(params_)
jacsim.evaluate()

#! For each of the 500 intervals, we have a 2-by-2 matrix as output
print "jacsim.output().shape = ", jacsim.output().shape

#! Show only the last 3 intervals.
print jacsim.output()[-3*2:,:]

Js = jacsim.output()[-2:,:]

# Assert that the two methods yield identical results
assert(sqrt(sumAll((Js - Ji)**2)) < 1e-4)

#! Monodromy matrix at various instances - Jacobian of ControlSimulator
#! ====================================================================

csim = ControlSimulator(f,linspace(0,tf,50))
csim.setOption("np",5)
csim.setOption("nf",10)
csim.setOption("integrator",CVodesIntegrator)
csim.setOption("integrator_options",{"reltol":1e-12,"abstol":1e-12})
csim.init()

jaccsim = Jacobian(csim,CONTROLSIMULATOR_X0,0)
jaccsim.setOption("ad_mode","forward")
jaccsim.init()
jaccsim.input(CONTROLSIMULATOR_P).set(params_[:-1])
jaccsim.input(CONTROLSIMULATOR_X0).set(x0)
jaccsim.input(CONTROLSIMULATOR_V).setAll(0)
jaccsim.evaluate()

#! For each of the 500 intervals, we have a 2-by-2 matrix as output
print "jaccsim.output().shape = ", jaccsim.output().shape

#! Show only the last 3 intervals.
print jaccsim.output()[-3*2:,:]
Jcs = jaccsim.output()[-2:,:]

# Assert that the two methods yield identical results
assert(sqrt(sumAll((Jcs - Js)**2)) < 1e-4)

#! Intuitive interpretation
#! ========================

sim.input(INTEGRATOR_X0).set(x0)
sim.input(INTEGRATOR_P).set(params_)
sim.evaluate()
unperturbed_output = DMatrix(sim.output())

circle = array([[sin(x),cos(x)] for x in numpy.linspace(-pi/2,3/2.0*pi,100)]).T
circle = hstack((circle,circle[:,50:51]))



for t in range(0,N/5,2):
  J = jacsim.output()[t*2:(t+1)*2,:]
  if t < 10:
    scale = 0.1
  else:
    scale = 0.01
  e=scale*mul(J,circle).T
  e[:,0] += sim.output()[t,0]
  e[:,1] += sim.output()[t,1]
  if t < 10 :
    plot(e[:,0],e[:,1],color='red')
  else:
    plot(e[:,0],e[:,1],color='blue')
    
show()
#! Consider the case of perturbation simulation with a slightly perturbed initial condition

sim.input(INTEGRATOR_X0).set(x0)
sim.input(INTEGRATOR_P).set(params_)
sim.evaluate()
unperturbed_output = DMatrix(sim.output())

perturb = DMatrix([1e-2,0])
sim.input(INTEGRATOR_X0).set(x0+perturb)
sim.input(INTEGRATOR_P).set(params_)
sim.evaluate()
perturbed_output = DMatrix(sim.output())

figure(2)

title('Evolution of a perturbation')
plot(ts,perturbed_output-unperturbed_output)

effects = DMatrix.zeros(N,2)

for t in range(N):
  effects[t,:] = mul(jacsim.output()[t*2:(t+1)*2,:],perturb).T
  
plot(ts,effects)

legend(('x_1','x_2','perturbed(x_1)','preturbed(y_2)'))
xlabel('t')

show()

figure(3)
linear_perturbed = unperturbed_output.reshape((2*N,1)) + mul(jacsim.output(),perturb)

title('phase portrait perturbation')
plot(unperturbed_output[:,0],unperturbed_output[:,1])
plot(perturbed_output[:,0],perturbed_output[:,1])
plot(linear_perturbed[0:N/2:2],linear_perturbed[1:N/2:2])

legend(('nominal','pertubed','monodromy prediction'))


show()

