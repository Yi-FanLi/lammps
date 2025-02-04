/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Package      FixDPPimd
   Purpose      A Path Integral Molecular Dynamics Package developed by DeepModeling community
   Authors      Yifan Li (mail_liyifan@163.com, yifanl@princeton.edu)

   Updated      Jul-02-2021
------------------------------------------------------------------------- */

#include "fix_dp_pimd.h"
#include <mpi.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include "universe.h"
#include "comm.h"
#include "neighbor.h"
#include "force.h"
#include "utils.h"
#include "timer.h"
#include "atom.h"
#include "group.h"
#include "compute.h"
#include "modify.h"
#include "domain.h"
#include "update.h"
#include "math_const.h"
#include "random_mars.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;
using namespace FixConst;
using namespace MathConst;

enum{PIMD,NMPIMD,CMD};
enum{physical, normal};
enum{baoab, obabo};
enum{SVR, PILE_L, PILE_G};
enum{MTTK, BZP};
enum{nve, nvt, nph, npt};
enum{MSTI, SCTI};

#define INVOKED_SCALAR 1

/* ---------------------------------------------------------------------- */

FixDPPimd::FixDPPimd(LAMMPS *lmp, int narg, char **arg) : 
  Fix(lmp, narg, arg),
  random(nullptr), c_pe(nullptr), c_press(nullptr), c_temp(nullptr)
{
  method        = NMPIMD;
  fmmode        = physical;
  integrator    = obabo;
  thermostat    = PILE_L;
  barostat      = BZP;
  ensemble      = nvt;
  fmass         = 1.0;
  temp          = 298.15;
  Lan_temp      = 298.15;
  sp            = 1.0;
  tau           = 1.0;
  tau_p         = 1.0;
  Pext          = 1.0;
  harmonicflag  = 0;
  omega         = 0.0;
  tiflag        = 0;
  timethod      = MSTI;
  lambda        = 0.0;
  pextflag      = 0;
  mapflag       = 1;
  removecomflag = 1;

  for(int i=3; i<narg-1; i+=2)
  {
    if(strcmp(arg[i],"method")==0)
    {
      if(strcmp(arg[i+1],"pimd")==0) method=PIMD;
      else if(strcmp(arg[i+1],"nmpimd")==0) method=NMPIMD;
      else if(strcmp(arg[i+1],"cmd")==0) method=CMD;
      else error->universe_all(FLERR,"Unknown method parameter for fix pimd");
    }

    else if(strcmp(arg[i], "integrator")==0)
    {
      if(strcmp(arg[i+1], "obabo")==0) integrator=obabo;
      else if(strcmp(arg[i+1], "baoab")==0) integrator=baoab;
      else error->universe_all(FLERR, "Unknown integrator parameter for fix pimd. Only obabo and baoab integrators is supported!");
    }

    else if(strcmp(arg[i], "ensemble")==0)
    {
      if(strcmp(arg[i+1], "nve")==0) ensemble=nve;
      else if(strcmp(arg[i+1], "nvt")==0) ensemble=nvt;
      else if(strcmp(arg[i+1], "nph")==0) {ensemble=nph; pextflag=1;}
      else if(strcmp(arg[i+1], "npt")==0) {ensemble=npt; pextflag=1;}
      else error->universe_all(FLERR, "Unknown ensemble parameter for fix pimd. Only nve ,nvt, nph, and npt ensembles are supported!");
    }

    else if(strcmp(arg[i],"fmass")==0)
    {
      fmass = atof(arg[i+1]);
      if(fmass<0.0 || fmass>1.0) error->universe_all(FLERR,"Invalid fmass value for fix pimd");
    }

    else if(strcmp(arg[i], "fmmode")==0)
    {
      if(strcmp(arg[i+1], "physical")==0) fmmode=physical;
      else if(strcmp(arg[i+1], "normal")==0) fmmode=normal;
      else error->universe_all(FLERR, "Unknown fictitious mass mode for fix pimd. Only physical mass and normal mode mass are supported!");
    }

    else if(strcmp(arg[i],"scale")==0)
    {
      pilescale = atof(arg[i+1]);
      if(pilescale<0.0) error->universe_all(FLERR,"Invalid pile scale value for fix pimd");
    }

    else if(strcmp(arg[i],"sp")==0)
    {
      sp = atof(arg[i+1]);
      if(fmass<0.0) error->universe_all(FLERR,"Invalid sp value for fix pimd");
    }

    else if(strcmp(arg[i],"temp")==0)
    {
      temp = atof(arg[i+1]);
      if(temp<0.0) error->universe_all(FLERR,"Invalid temp value for fix pimd");
    } 

    else if(strcmp(arg[i], "thermostat")==0)
    {
      if(strcmp(arg[i+1],"PILE_G")==0) 
      {
        thermostat = PILE_G;
        seed = atoi(arg[i+2]);
        i++;
      }
      else if(strcmp(arg[i+1], "SVR")==0)
      {
        thermostat = SVR;
        seed = atoi(arg[i+2]);
        i++;
      }
      else if(strcmp(arg[i+1],"PILE_L")==0) 
      {
        thermostat = PILE_L;
        seed = atoi(arg[i+2]);
        i++;
      }
      else error->universe_all(FLERR,"Unknown thermostat parameter for fix pimd");
    }

    else if(strcmp(arg[i], "tau")==0)
    {
      tau = atof(arg[i+1]);
    }
  
    else if(strcmp(arg[i], "press")==0)
    {
      Pext = atof(arg[i+1]);
      if(Pext<0.0) error->universe_all(FLERR,"Invalid press value for fix pimd");
    }

    else if(strcmp(arg[i], "barostat")==0)
    {
      if(strcmp(arg[i+1],"MTTK")==0) 
      {
        barostat = MTTK;
      }
      else if(strcmp(arg[i+1],"BZP")==0)
      {
        barostat = BZP;
      }
      else error->universe_all(FLERR,"Unknown barostat parameter for fix pimd");
    }

    else if(strcmp(arg[i], "taup")==0)
    {
      tau_p = atof(arg[i+1]);
      if(tau_p<=0.0) error->universe_all(FLERR, "Invalid tau_p value for fix pimd");
    }

    else if(strcmp(arg[i], "ti")==0)
    {
      tiflag = 1;
      if(strcmp(arg[i+1], "MSTI")==0)  timethod = MSTI;
      else if(strcmp(arg[i+1], "SCTI")==0)  timethod = SCTI;
      else error->universe_all(FLERR, "Unknown method parameter for thermodynamic integration");
      lambda = atof(arg[i+2]);
      i++;
    }
 
    else if(strcmp(arg[i], "model")==0)
    {
      harmonicflag = 1;
      omega = atof(arg[i+1]);
      if(omega<0) error->universe_all(FLERR,"Invalid model frequency value for fix pimd");
    }

    else if(strcmp(arg[i], "fixcom")==0)
    {
      if(strcmp(arg[i+1], "no")==0) removecomflag = 1;
      else if(strcmp(arg[i+1], "yes")==0) removecomflag = 0;
    }

    else if(strcmp(arg[i], "map")==0)
    {
      if(strcmp(arg[i+1], "yes")==0) mapflag = 1;
      else if(strcmp(arg[i+1], "no")==0) mapflag = 0;
    }
    else error->universe_all(arg[i],i+1,"Unknown keyword for fix pimd");
  }

  vol0 = domain->xprd * domain->yprd * domain->zprd;
  omega_dot = nullptr;

  // initialize Marsaglia RNG with processor-unique seed

  if(integrator==baoab || integrator==obabo)
  {
    Lan_temp = temp;
    random = new RanMars(lmp, seed + universe->me);
  }
  
  /* Initiation */

  max_nsend = 0;
  tag_send = nullptr;
  buf_send = nullptr;

  max_nlocal = 0;
  buf_recv = nullptr;
  buf_beads = nullptr;

  coords_send = coords_recv = nullptr;
  forces_send = forces_recv = nullptr;
  nsend = nrecv = 0;
  tags_send = nullptr;
  coords = nullptr;
  forces = nullptr;
  size_plan = 0;
  plan_send = plan_recv = nullptr;

  xc = fc = nullptr;
  xf = 0.0;
  t_vir = t_cv = 0.0;
  total_spring_energy = 0.0;
  t_prim = 0.0;

  for(int i=0; i<9; i++) virial[i] = 0.0;

  tote = totke = totenthalpy = 0.0;
  ke_bead = 0.0;

  dfdl = 0.0;
  x_scaled = nullptr;

  M_x2xp = M_xp2x = nullptr; // M_f2fp = M_fp2f = nullptr;
  lam = nullptr;
  mode_index = nullptr;
  x_unwrap = nullptr;

  mass = nullptr;

  array_atom = nullptr;

  gamma = 0.0;
  c1 = 0.0;
  c2 = 0.0;

  restart_peratom = 1;
  peratom_flag    = 1;
  peratom_freq    = 1;

  global_freq = 1;
  thermo_energy = 1;
  vector_flag = 1;
  size_vector = 13;
  scalar_flag = 0;
  extvector   = 1;
  comm_forward = 3;

  atom->add_callback(0); // Call LAMMPS to allocate memory for per-atom array
  atom->add_callback(1); // Call LAMMPS to re-assign restart-data for per-atom array


  // some initilizations

  baoab_ready = false;

  r1 = 0.0;
  r2 = 0.0;

  id_temp = utils::strdup(std::string(id) + "_temp");
  modify->add_compute(fmt::format("{} all temp",id_temp));

  id_pe = new char[8];
  strcpy(id_pe, "pimd_pe");
  char **newarg = new char*[3];
  newarg[0] = id_pe;
  newarg[1] = (char *) "all";
  newarg[2] = (char *) "pe";
  modify->add_compute(3,newarg);
  delete [] newarg;

  id_press = new char[12];
  strcpy(id_press, "pimd_press");
  newarg = new char*[5];
  newarg[0] = id_press;
  newarg[1] = (char*) "all";
  newarg[2] = (char*) "pressure";
  newarg[3] = (char*) "thermo_temp";
  newarg[4] = (char*) "virial";
  modify->add_compute(5, newarg);
  delete [] newarg;
  
  // id_press2 = new char[10];
  // strcpy(id_press2, "pi_press");
  // newarg = new char*[4];
  // newarg[0] = id_press2;
  // newarg[1] = (char*) "all";
  // newarg[2] = (char*) "pressure";
  // // newarg[3] = (char*) "thermo_temp";
  // newarg[3] = id_temp;
  // modify->add_compute(4, newarg);
  // delete [] newarg;

  domain->set_global_box();

 
  //FILE *frand;
  //std::string fname = "rand_";
  //fname += std::to_string(universe->iworld);
  //fname += ".txt";
  //frand = fopen(fname.c_str(), "w");
}

/* ---------------------------------------------------------------------- */

FixDPPimd::~FixDPPimd()
{
  delete _omega_k;
  delete Lan_c, Lan_s;
  if(integrator==baoab)
  {
    delete random;
  }

  if(thermostat==PILE_L)
  {
    delete tau_k ,c1_k, c2_k;
  }
  //fclose(frand);
}

/* ---------------------------------------------------------------------- */

int FixDPPimd::setmask()
{
  int mask = 0;
  //mask |= PRE_EXCHANGE;
  // mask |= PRE_FORCE;
  mask |= POST_FORCE;
  mask |= INITIAL_INTEGRATE;
  mask |= FINAL_INTEGRATE;
  mask |= END_OF_STEP;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::end_of_step()
{
  // compute_totke();
  // compute_vir();
  // compute_vir_();
  compute_totke();
  inv_volume = 1.0 / (domain->xprd * domain->yprd * domain->zprd);
  compute_p_prim();
  compute_p_cv();
  compute_tote();
  if(pextflag) compute_totenthalpy();

  if(update->ntimestep % 10000 == 0)
  {
  if(universe->me==0) printf("This is the end of step %ld.\n", update->ntimestep);
  }

}

/* ---------------------------------------------------------------------- */

void FixDPPimd::init()
{
  // flog.open(std::to_string(universe->me)+std::string(".flog"), std::fstream::in | std::fstream::out);
  // flog = fopen((std::to_string(universe->me)+std::string(".flog")).c_str(), "w");
  if (atom->map_style == 0)
    error->all(FLERR,"Fix pimd requires an atom map, see atom_modify");

  if(universe->me==0 && screen) fprintf(screen,"Fix pimd initializing Path-Integral ...\n");
  // fprintf(stdout, "Fix pimd initilizing Path-Integral ...\n");

  masstotal = group->mass(igroup);
  // prepare the constants

  np = universe->nworlds;
  inverse_np = 1.0 / np;

  /* The first solution for the force constant, using SI units

  const double Boltzmann = 1.3806488E-23;    // SI unit: J/K
  const double Plank     = 6.6260755E-34;    // SI unit: m^2 kg / s

  double hbar = Plank / ( 2.0 * MY_PI ) * sp;
  double beta = 1.0 / ( Boltzmann * input.nh_temp);

  // - P / ( beta^2 * hbar^2)   SI unit: s^-2
  double _fbond = -1.0 / (beta*beta*hbar*hbar) * input.nbeads;

  // convert the units: s^-2 -> (kcal/mol) / (g/mol) / (A^2)
  fbond = _fbond * 4.184E+26;

  */

  /* The current solution, using LAMMPS internal real units */

  const double Boltzmann = force->boltz;
  const double Plank     = force->hplanck;

  // double hbar   = Plank / ( 2.0 * MY_PI );
  hbar = force->hplanck;
  kBT = Boltzmann * temp;
  beta   = 1.0 / kBT;
  double _fbond = 1.0 * np*np / (beta*beta*hbar*hbar) ;

  omega_np = np / (hbar * beta) * sqrt(force->mvv2e);
  fbond = _fbond * force->mvv2e;

  beta_np = 1.0 / force->boltz / Lan_temp / np;

  if(universe->me==0)
    printf("Fix pimd -P/(beta^2 * hbar^2) = %20.7lE (kcal/mol/A^2)\n\n", fbond);

  if(integrator==obabo)
  {
    dtf = 0.5 * update->dt * force->ftm2v;
    dtv = 0.5 * update->dt;
    dtv2 = dtv * dtv;
    dtv3 = 1./3 * dtv2 * dtv * force->ftm2v;
  }
  else if(integrator==baoab)
  {
    dtf = 0.5 * update->dt * force->ftm2v;
    dtv = 0.5 * update->dt;
    dtv2 = dtv * dtv;
    dtv3 = 1./3 * dtv2 * dtv * force->ftm2v;
  }
  else
  {
    error->universe_all(FLERR,"Unknown integrator parameter for fix pimd");
  }

  // n_unwrap = atom->nlocal + 200;
  // if(x_unwrap) delete [] x_unwrap;
  // x_unwrap = new double*[n_unwrap];
  // for(int i=0; i<n_unwrap; i++) 
  // {
    // x_unwrap[i] = new double[3];
    // x_unwrap[i] = (double*) memory->smalloc(sizeof(double)*3, "FixDPPimd:x_unwrap[i]");
    // x_unwrap[i][0] = x_unwrap[i][1] = x_unwrap[i][2] = 0.0;
  // }
  // printf("x_unwrap initialized\n");


  comm_init();

  mass = new double [atom->ntypes+1];

  nmpimd_init();
  // if(method==CMD || method==NMPIMD) nmpimd_init();
  // else for(int i=1; i<=atom->ntypes; i++) mass[i] = atom->mass[i] / np * fmass;

  if(integrator==baoab || integrator==obabo)
  {
    if(!baoab_ready)
    {
      Langevin_init();
    }
    // fprintf(stdout, "baoab thermostat initialized!\n");
  }
  else error->universe_all(FLERR,"Unknown integrator parameter for fix pimd");

  if(pextflag)
  {
    W = 3 * (atom->natoms) * tau_p * tau_p * np * kBT; // consistent with the definition in i-Pi
    //W = 4 * tau_p * tau_p / beta_np;
    //printf("N=%d, tau_p=%f, beta=%f, W=%f\n", atom->natoms, tau_p, beta_np, W);
    Vcoeff = 1.0;
    // if(removecomflag) Vcoeff = 2.0;
    // else if(!removecomflag) Vcoeff = 1.0;
    vw = 0.0;
    omega_dot = new double[3];
    for(int i=0; i<3; i++) omega_dot[i] = 0.0;
  }

  // int itemp = modify->find_compute(id_temp);
  // if (itemp < 0)
  //   error->all(FLERR,"Temperature ID for fix nvt/npt does not exist");
  // c_temp = modify->compute[itemp];

  // initialize compute pe 
  int ipe = modify->find_compute(id_pe);
  c_pe = modify->compute[ipe];
  
  // initialize compute press
  int ipress = modify->find_compute(id_press);
  c_press = modify->compute[ipress];

  // int ipress2 = modify->find_compute(id_press2);
  // c_press2 = modify->compute[ipress2];

  t_prim = t_vir = t_cv = p_prim = p_vir = p_cv = p_md = 0.0;

  if(universe->me==0) fprintf(screen, "Fix pimd successfully initialized!\n");
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::setup(int vflag)
{
  int nlocal = atom->nlocal;
  tagint *tag = atom->tag;
  double **x = atom->x;
  imageint *image = atom->image;
  // t_current = c_temp->compute_scalar();
  // tdof = c_temp->dof;
  // printf("setup, m = %.4e\n", mass[1]);
  if(mapflag){
    for(int i=0; i<nlocal; i++)
    {
      domain->unmap(x[i], image[i]);
    }
  }
  if(method==NMPIMD)
  {
    MPI_Barrier(universe->uworld);
    nmpimd_fill(atom->x);
    MPI_Barrier(universe->uworld);
    comm_exec(atom->x);
    MPI_Barrier(universe->uworld);
    nmpimd_transform(buf_beads, atom->x, M_x2xp[universe->iworld]);
  }
  compute_spring_energy();
  if(method==NMPIMD)
  {
    MPI_Barrier(universe->uworld);
    nmpimd_fill(atom->x);
    MPI_Barrier(universe->uworld);
    comm_exec(atom->x);
    MPI_Barrier(universe->uworld);
    nmpimd_transform(buf_beads, atom->x, M_xp2x[universe->iworld]);
  }

  if(method==NMPIMD)
  {
    MPI_Barrier(universe->uworld);
    nmpimd_fill(atom->v);
    MPI_Barrier(universe->uworld);
    comm_exec(atom->v);
    MPI_Barrier(universe->uworld);
    nmpimd_transform(buf_beads, atom->v, M_x2xp[universe->iworld]);
  }
  compute_xc();
  update_x_unwrap();
  if(mapflag)
  {
    for(int i=0; i<nlocal; i++)
    {
      domain->unmap_inv(x[i], image[i]);
    }
  }
  // printf("setting up %d\n", vflag);
  if(universe->me==0 && screen) fprintf(screen,"Setting up Path-Integral ...\n");
  if(universe->me==0) printf("Setting up Path-Integral ...\n");
  // printf("setting up, m = %.4e\n", mass[1]);
  post_force(vflag);
  // printf("after post_force, m = %.4e\n", mass[1]);
  // printf("after post_force\n");
  compute_totke();
  compute_pote();
  // compute_p_cv();
  //compute_p_vir();
  end_of_step();
  // printf("me = %d, after end of step\n", universe->me);
  //fprintf(stdout, "virial=%.8e.\n", virial[0]+virial[4]+virial[8]);
  //fprintf(stdout, "vir=%.8e.\n", vir);
  c_pe->addstep(update->ntimestep+1); 
  c_press->addstep(update->ntimestep+1);
  double *boxlo = domain->boxlo;
  double *boxhi = domain->boxhi;
  //fprintf(stdout, "%.8e, %.8e, %.8e, %.8e, %.8e, %.8e\n", boxlo[0], boxlo[1], boxlo[2], boxhi[0], boxhi[1], boxhi[2]);

  //fprintf(stdout, "x=%.4e.\n", atom->x[0][0]);  
  vol_ = domain->xprd * domain->yprd * domain->zprd;
  
/*
  // Make the initial force zero so that it matches i-PI. 
  // Only for debug purpose.
  int nlocal = atom->nlocal;
  double **x = atom->x;
  double **v = atom->v;
  double **f = atom->f;
  tagint *tag = atom->tag;

  // printf("start qc_step, x:\n");
  for(int i=0; i<nlocal; i++)
  {
    // printf("%ld  ", tag[i]);
    for(int j=0; j<3; j++)
    {
      // printf("%.8e  ", x[i][j]);
      f[i][j] = 0.0;
    }
    // printf("\n");
  }
  // printf("\n");
*/
  // printf("me = %d, Successfully set up!\n", universe->me);
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::initial_integrate(int /*vflag*/)
{
    double vnorm1 = 0.0, vnorm2 = 0.0, vnorm1a = 0.0; 
    double xnorm1 = 0.0, xnorm2 = 0.0, xnorm1a = 0.0; 

  // t_current = c_temp->compute_scalar();
  // printf("me = %d, initial\n", universe->me);
  // unmap the atom coordinates and image flags so that the ring polymer is not wrapped
  int nlocal = atom->nlocal;
  tagint *tag = atom->tag;
  double **x = atom->x;
  imageint *image = atom->image;

  if(mapflag){
    for(int i=0; i<nlocal; i++)
    {
      // fprintf(stdout, "i=%d, tag=%d\n", i, tag[i]);
      domain->unmap(x[i], image[i]);
    }
  }
  
  if(integrator==obabo)
  {
    // printf("before press_v_step, vw = %.30e\n", vw);

    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     vnorm1 += atom->v[i][j];
    //     vnorm1a += abs(atom->v[i][j]);
    //     vnorm2 += atom->v[i][j] * atom->v[i][j];
    //   }
    // }

    // printf("before press_v_step, v:\n");
    // printf("vnorm1: %.30e\n", vnorm1);
    // printf("vnorm1a: %.30e\n", vnorm1a);
    // printf("vnorm2: %.30e\n", vnorm2);
    if(ensemble==nvt || ensemble==npt)
    {
      o_step();
      if(removecomflag) remove_com_motion();
      if(pextflag) press_o_step();
    }
    compute_totke();
    compute_p_cv();
    if(pextflag) 
    {
      press_v_step();
    //   v_press_step();
    }

    // vnorm1 = vnorm1a = vnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     vnorm1 += atom->v[i][j];
    //     vnorm1a += abs(atom->v[i][j]);
    //     vnorm2 += atom->v[i][j] * atom->v[i][j];
    //   }
    // }

    // printf("before b_step, v:\n");
    // printf("vnorm1: %.30e\n", vnorm1);
    // printf("vnorm1a: %.30e\n", vnorm1a);
    // printf("vnorm2: %.30e\n", vnorm2);

    b_step();
    if(removecomflag) remove_com_motion();
    if(method==NMPIMD)
    {
      MPI_Barrier(universe->uworld);
      nmpimd_fill(atom->x);
      MPI_Barrier(universe->uworld);
      comm_exec(atom->x);
      MPI_Barrier(universe->uworld);
      nmpimd_transform(buf_beads, atom->x, M_x2xp[universe->iworld]);
    }

    // vnorm1 = vnorm1a = vnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     vnorm1 += atom->v[i][j];
    //     vnorm1a += abs(atom->v[i][j]);
    //     vnorm2 += atom->v[i][j] * atom->v[i][j];
    //   }
    // }

    // printf("before remap, v:\n");
    // printf("vnorm1: %.30e\n", vnorm1);
    // printf("vnorm1a: %.30e\n", vnorm1a);
    // printf("vnorm2: %.30e\n", vnorm2);



    // if(pextflag) 
    // {
    //   x_press_step();
    //   press_remap();

    // xnorm1 = xnorm1a = xnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("after remap1, x:\n");
    // printf("xnorm1: %.30e\n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("xnorm2: %.30e\n", xnorm2);
    // }
    qc_step();

    // xnorm1 = xnorm1a = xnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("\n\niworld = %d, step = %d\n", universe->iworld, update->ntimestep);
    // printf("before a_step, x:\n");
    // printf("xnorm1: %.30e\n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("iworld = %d xnorm2: %.30e\n", universe->iworld, xnorm2);

    // vnorm1 = vnorm1a = vnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     vnorm1 += atom->v[i][j];
    //     vnorm1a += abs(atom->v[i][j]);
    //     vnorm2 += atom->v[i][j] * atom->v[i][j];
    //   }
    // }

    // printf("before a_step, v:\n");
    // printf("vnorm1: %.30e\n", vnorm1);
    // printf("vnorm1a: %.30e\n", vnorm1a);
    // printf("iworld = %d vnorm2: %.30e\n", universe->iworld, vnorm2);
    a_step();
    qc_step();
    a_step();

    // xnorm1 = xnorm1a = xnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("after a_step, x:\n");
    // printf("xnorm1: %.30e\n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("iworld = %d, xnorm2: %.30e\n", universe->iworld, xnorm2);

    // vnorm1 = vnorm1a = vnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     vnorm1 += atom->v[i][j];
    //     vnorm1a += abs(atom->v[i][j]);
    //     vnorm2 += atom->v[i][j] * atom->v[i][j];
    //   }
    // }

    // printf("after a_step, v:\n");
    // printf("vnorm1: %.30e\n", vnorm1);
    // printf("vnorm1a: %.30e\n", vnorm1a);
    // printf("iworld = %d vnorm2: %.30e\n", universe->iworld, vnorm2);

    // if(pextflag) 
    // {
    //   x_press_step();
    //   press_remap();
    // }

    // xnorm1 = xnorm1a = xnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("after remap, x:\n");
    // printf("xnorm1: %.30e\n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("xnorm2: %.30e\n", xnorm2);
    // printf("me = %d, stepped\n", universe->me);
  }

  else if(integrator==baoab)
  {
    if(pextflag) press_v_step();
    b_step();
    if(method==NMPIMD)
    {
      MPI_Barrier(universe->uworld);
      nmpimd_fill(atom->x);
      MPI_Barrier(universe->uworld);
      comm_exec(atom->x);
      MPI_Barrier(universe->uworld);
      nmpimd_transform(buf_beads, atom->x, M_x2xp[universe->iworld]);
    }
    qc_step();
    a_step();
    if(ensemble==nvt || ensemble==npt)
    {
      o_step();
      if(removecomflag) remove_com_motion();
      if(pextflag) press_o_step();
    }
    qc_step();
    a_step();
  }

  else
  {
    error->universe_all(FLERR,"Unknown integrator parameter for fix pimd");
  }

    compute_spring_energy();

    // xnorm1 = xnorm1a = xnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("before xp2x, x:\n");
    // printf("xnorm1: %.30e\n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("iworld = %d, xnorm2: %.30e\n", universe->iworld, xnorm2);

    if(method==NMPIMD)
    {
      MPI_Barrier(universe->uworld);
      nmpimd_fill(atom->x);

      // printf("me = %d\n", universe->me);
      // for(int i=0; i<2; i++)
      // {
      //   printf("tag = %d\n", atom->tag[i]);
      //   for(int j=0; j<3; j++)
      //   {
      //     printf("%.4e ", atom->x[i][j]);
      //   }
      //   printf("\n");
      // }

      MPI_Barrier(universe->uworld);
      comm_exec(atom->x);

      // printf("me = %d, buf_beads:\n", universe->me);

      // // printf("me = %d\n", universe->me);
      // for(int i=0; i<2; i++)
      // {
      //   printf("tag = %d ", atom->tag[i]);
      //   for(int j=0; j<3; j++) printf("%.4e ", atom->x[i][j]);
      //   printf("\n");
      //   for(int k=0; k<np; k++)
      //   {
      //     for(int j=0; j<3; j++)
      //     {
      //       printf("%.4e ", buf_beads[k][3*i+j]);
      //     }
      //   }
      //   printf("\n");
      // }

      MPI_Barrier(universe->uworld);
      nmpimd_transform(buf_beads, atom->x, M_xp2x[universe->iworld]);
    }

    // xnorm1 = xnorm1a = xnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("after xp2x, x:\n");
    // printf("xnorm1: %.30e\n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("iworld = %d, xnorm2: %.30e\n", universe->iworld, xnorm2);
    // printf("computing xc\n");
    // printf("xc computed\n");
    // MPI_Barrier(universe->uworld);
    // update_x_unwrap();
    // MPI_Barrier(universe->uworld);
    // compute_xc();
    // printf("x_unwrap updated\n");

    if(mapflag)
    {
      // printf("REMAPPING!\n");
      for(int i=0; i<nlocal; i++)
      {
        // fprintf(stdout, "i=%d, tag=%d\n", i, tag[i]);
        domain->unmap_inv(x[i], image[i]);
      }
    }

    // xnorm1 = xnorm1a = xnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("\n\n end of initial_integrate, x:\n");
    // printf("xnorm1: %.30e\n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("iworld = %d, xnorm2: %.30e\n", universe->iworld, xnorm2);
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::final_integrate()
{
    // double fnorm1 = 0.0, fnorm2 = 0.0, fnorm1a = 0.0; 
    // fnorm1 = fnorm1a = fnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     fnorm1 += atom->f[i][j];
    //     fnorm1a += abs(atom->f[i][j]);
    //     fnorm2 += atom->f[i][j] * atom->f[i][j];
    //   }
    // }

    // printf("before b_step, f:\n");
    // printf("fnorm1: %.30e\n", fnorm1);
    // printf("fnorm1a: %.30e\n", fnorm1a);
    // printf("iworld = %d, fnorm2: %.30e\n", universe->iworld, fnorm2);

  if(pextflag) {
    // v_press_step();
    compute_totke();
    compute_p_cv();
    press_v_step();
  }
  b_step();
  if(removecomflag) remove_com_motion();
  // t_current = c_temp->compute_scalar();
  // c_temp->compute_scalar();


  if(integrator==obabo)
  {
    if(ensemble==nvt || ensemble==npt)
    {
      o_step();
      if(removecomflag) remove_com_motion();
      if(pextflag) press_o_step();
    }
  }
  else if(integrator==baoab)
  {

  }
  else
  {
    error->universe_all(FLERR,"Unknown integrator parameter for fix pimd");
  }
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::update_x_unwrap()
{
  MPI_Barrier(universe->uworld); 
  int nlocal = atom->nlocal;
  double **x = atom->x;
  // printf("updating x_unwrap!\n");
  // if(nlocal > n_unwrap) 
  // {
  //   n_unwrap = nlocal + 200;
    // for(int i=0; i<n_unwrap; i++) delete [] x_unwrap[i];
    delete [] x_unwrap;
    x_unwrap = nullptr;
    // for(int i=0; i<n_unwrap; i++) x_unwrap[i] = nullptr;

    // x_unwrap = (double**) memory->srealloc(x_unwrap, sizeof(double*)*n_unwrap, "FixDPPimd::x_unwrap");
    // printf("trying to realloc x_u\n");
    // x_unwrap = new double*[max_nlocal];
    // for(int i=0; i<n_unwrap; i++) 
    // {
      // printf("x_unw[%d]\n", i);
      // x_unwrap[i] = new double[3];
      x_unwrap = (double*) memory->srealloc(x_unwrap, sizeof(double)*(nlocal+200)*3, "FixDPPimd::x_unwrap");
    // }
  // }
  // printf("trying to copy\n");
  // fprintf(flog, "in update_x_upwrap, nlocal = %d\n", nlocal);
  for(int i=0; i<nlocal; i++)
  { 
    // printf("%.4e %.4e %.4e\n", x_unwrap[i][0], x_unwrap[i][1], x_unwrap[i][2]);
    // memcpy(&(x_unwrap[i][0]), &(x[i][0]), sizeof(double)*3);
    for(int j=0; j<3; j++)
    {
      // fprintf(flog, "i = %d ", i);
      x_unwrap[3*i+j] = x[i][j];
      // fprintf(flog, " %.4e ", x_unwrap[3*i+j]);
    }
    // fprintf(flog, "\n");
  }
  MPI_Barrier(universe->uworld); 
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::post_force(int /*flag*/)
{
  int nlocal = atom->nlocal;
  tagint *tag = atom->tag;
  double **x = atom->x;
  imageint *image = atom->image;
  if(mapflag){
    for(int i=0; i<nlocal; i++)
    {
      domain->unmap(x[i], image[i]);
    }
  }
  MPI_Barrier(universe->uworld);
  update_x_unwrap();
  MPI_Barrier(universe->uworld);
  compute_xc();
  if(mapflag)
  {
    for(int i=0; i<nlocal; i++)
    {
      domain->unmap_inv(x[i], image[i]);
    }
  }
    // double fnorm1 = 0.0, fnorm2 = 0.0, fnorm1a = 0.0; 
    // fnorm1 = fnorm1a = fnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     fnorm1 += atom->f[i][j];
    //     fnorm1a += abs(atom->f[i][j]);
    //     fnorm2 += atom->f[i][j] * atom->f[i][j];
    //   }
    // }

    // printf("checking f for step %d iworld %d\n", update->ntimestep, universe->iworld);
    // printf("starting post_force, f:\n");
    // printf("fnorm1: %.30e\n", fnorm1);
    // printf("fnorm1a: %.30e\n", fnorm1a);
    // printf("iworld = %d, fnorm2: %.30e\n", universe->iworld, fnorm2);

    // double fsnorm1 = 0.0, fsnorm2 = 0.0, fsnorm1a = 0.0; 
    // fsnorm1 = fsnorm1a = fsnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal+atom->nghost; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     fsnorm1 += atom->f[i][j];
    //     fsnorm1a += abs(atom->f[i][j]);
    //     fsnorm2 += atom->f[i][j] * atom->f[i][j];
    //   }
    // }
    // printf("fsnorm1: %.30e\n", fsnorm1);
    // printf("fsnorm1a: %.30e\n", fsnorm1a);
    // printf("iworld = %d, fsnorm2: %.30e\n", universe->iworld, fsnorm2);

  // unmap the atom coordinates and image flags so that the ring polymer is not wrapped
  // int nlocal = atom->nlocal;
  // double **x = atom->x;
  // imageint *image = atom->image;
  // for(int i=0; i<nlocal; i++)
  // {
  //   domain->unmap(x[i], image[i]);
  // }

  // compute_fc();
  compute_vir();
  // printf("vir computed\n");
  compute_vir_();
  // printf("vir_ computed\n");
  compute_t_prim();
  compute_t_vir();
  compute_pote();
  
    // double xnorm1 = 0.0, xnorm2 = 0.0, xnorm1a = 0.0; 
    // double xsnorm1 = 0.0, xsnorm2 = 0.0, xsnorm1a = 0.0; 
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xnorm1 += atom->x[i][j];
    //     xnorm1a += abs(atom->x[i][j]);
    //     xnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // for(int i=0; i<atom->nlocal+atom->nghost; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     xsnorm1 += atom->x[i][j];
    //     xsnorm1a += abs(atom->x[i][j]);
    //     xsnorm2 += atom->x[i][j] * atom->x[i][j];
    //   }
    // }

    // printf("checking x xor step %d iworld %d\n", update->ntimestep, universe->iworld);
    // printf("bexore x2xp, x:\n");
    // printf("xnorm1: %.30e \n", xnorm1);
    // printf("xnorm1a: %.30e\n", xnorm1a);
    // printf("iworld = %d, xnorm2: %.30e\n", universe->iworld, xnorm2);

    // printf("xsnorm1: %.30e \n", xsnorm1);
    // printf("xsnorm1a: %.30e\n", xsnorm1a);
    // printf("iworld = %d, xsnorm2: %.30e\n", universe->iworld, xsnorm2);

    // double fnorm1 = 0.0, fnorm2 = 0.0, fnorm1a = 0.0; 
    // fnorm1 = fnorm1a = fnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     fnorm1 += atom->f[i][j];
    //     fnorm1a += abs(atom->f[i][j]);
    //     fnorm2 += atom->f[i][j] * atom->f[i][j];
    //   }
    // }

    // printf("checking f for step %d iworld %d\n", update->ntimestep, universe->iworld);
    // printf("before f2fp, f:\n");
    // printf("fnorm1: %.30e\n", fnorm1);
    // printf("fnorm1a: %.30e\n", fnorm1a);
    // printf("iworld = %d, fnorm2: %.30e\n", universe->iworld, fnorm2);
  // transform the forces into normal mode representation
  if(method==NMPIMD)
  {
    MPI_Barrier(universe->uworld);
    nmpimd_fill(atom->f);
    MPI_Barrier(universe->uworld);
    comm_exec(atom->f);
    MPI_Barrier(universe->uworld);
    nmpimd_transform(buf_beads, atom->f, M_x2xp[universe->iworld]);
  }
    // fnorm1 = fnorm1a = fnorm2 = 0.0;
    // for(int i=0; i<atom->nlocal; i++)
    // {
    //   for(int j=0; j<3; j++)
    //   {
    //     fnorm1 += atom->f[i][j];
    //     fnorm1a += abs(atom->f[i][j]);
    //     fnorm2 += atom->f[i][j] * atom->f[i][j];
    //   }
    // }

    // printf("after f2fp, f:\n");
    // printf("fnorm1: %.30e\n", fnorm1);
    // printf("fnorm1a: %.30e\n", fnorm1a);
    // printf("iworld = %d, fnorm2: %.30e\n", universe->iworld, fnorm2);
  c_pe->addstep(update->ntimestep+1); 
  c_press->addstep(update->ntimestep+1); 
}

/* ----------------------------------------------------------------------
   NM propagator and Langevin thermostat initialization
------------------------------------------------------------------------- */

void FixDPPimd::Langevin_init()
{
  //fprintf(stdout, "Lan_temp=%.2f.\n", Lan_temp);
  double KT = force->boltz * Lan_temp;
  double beta = 1.0 / KT;
  // double hbar = force->hplanck / (2.0 * MY_PI);
  _omega_np = np / beta / hbar;
  double _omega_np_dt_half = _omega_np * update->dt * 0.5;

  _omega_k = new double[np];
  Lan_c = new double[np];
  Lan_s = new double[np];
  if(fmmode==physical){
    for (int i=0; i<np; i++)
    {
      _omega_k[i] = _omega_np * sqrt(lam[i]); 
      Lan_c[i] = cos(sqrt(lam[i])*_omega_np_dt_half);
      Lan_s[i] = sin(sqrt(lam[i])*_omega_np_dt_half);
      // printf("i=%d w=%.8e\nc=%.8e\ns=%.8e\n", i, _omega_k[i], Lan_c[i], Lan_s[i]);
    }
  }
  else if(fmmode==normal){
    for (int i=0; i<np; i++)
    {
      _omega_k[i] = _omega_np; 
      Lan_c[i] = cos(_omega_np_dt_half);
      Lan_s[i] = sin(_omega_np_dt_half);
    }
  }
  if(tau > 0) gamma = 1.0 / tau;
  else gamma = np / beta / hbar;
  
  if(integrator==obabo) c1 = exp(-gamma * 0.5 * update->dt); // tau is the damping time of the centroid mode.
  else if(integrator==baoab) c1 = exp(-gamma * update->dt); 
  else error->universe_all(FLERR, "Unknown integrator parameter for fix pimd. Only obabo and baoab integrators is supported!");

  c2 = sqrt(1.0 - c1 * c1); // note that c1 and c2 here only works for the centroid mode.

  if(thermostat == PILE_L || thermostat == PILE_G)
  {
    std::string out = "\nInitializing PI Langevin equation thermostat...\n";
    out += "Bead ID    |    omega    |    tau    |    c1    |    c2\n"; 
    //if(universe->iworld==0) fprintf(stdout, "Initializing PILE_L thermostat.\n");
    tau_k = new double[np];
    c1_k = new double[np];
    c2_k = new double[np];
    tau_k[0] = tau; c1_k[0] = c1; c2_k[0] = c2;
    for(int i=1; i<np; i++)
    {
      tau_k[i] = 0.5 / pilescale / _omega_k[i];
      if(integrator==obabo) c1_k[i] = exp(-0.5 * update->dt / tau_k[i]);
      else if(integrator==baoab) c1_k[i] = exp(-1.0 * update->dt / tau_k[i]);
      else error->universe_all(FLERR, "Unknown integrator parameter for fix pimd. Only obabo and baoab integrators is supported!");
      c2_k[i] = sqrt(1.0 - c1_k[i] * c1_k[i]);
    }
    for(int i=0; i<np; i++)
    {
      out += fmt::format("    {:d}     {:.8e} {:.8e} {:.8e} {:.8e}\n", i, _omega_k[i], tau_k[i], c1_k[i], c2_k[i]);
    }
    if(thermostat == PILE_L) out += "PILE_L thermostat successfully initialized!\n";
    else if(thermostat == PILE_G) out += "PILE_G thermostat successfully initialized!\n";
    out += "\n";
    utils::logmesg(lmp, out);
  }


  baoab_ready = true;
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::b_step()
{

  int n = atom->nlocal;
  int *type = atom->type;
  double **v = atom->v;
  double **f = atom->f;

  // double vnorm1 = 0.0, vnorm2 = 0.0; 
  // for(int i=0; i<atom->nlocal; i++)
  // {
  //   for(int j=0; j<3; j++)
  //   {
  //     vnorm1 += atom->v[i][j];
  //     vnorm2 += atom->v[i][j] * atom->v[i][j];
  //   }
  // }

  // printf("start b_step, iworld = %d, v:\n", universe->iworld);
  // printf("vnorm1: %.16e\n", vnorm1);
  // printf("vnorm2: %.16e\n", vnorm2);

  // double fnorm1 = 0.0, fnorm2 = 0.0; 
  // for(int i=0; i<atom->nlocal; i++)
  // {
  //   for(int j=0; j<3; j++)
  //   {
  //     fnorm1 += atom->f[i][j];
  //     fnorm2 += atom->f[i][j] * atom->f[i][j];
  //   }
  // }

  // printf("start b_step, iworld = %d, f:\n", universe->iworld);
  // printf("fnorm1: %.16e\n", fnorm1);
  // printf("fnorm2: %.16e\n", fnorm2);
  // if(universe->iworld==0) 
  // {
  //   printf("start b_step, v:\n");
  //   for(int i=0; i<atom->nlocal; i++)
  //   {
  //     printf("%ld  ", atom->tag[i]);
  //     for(int j=0; j<3; j++)
  //     {
  //       printf("%.8e  ", atom->v[i][j]);
  //     }
  //     printf("\n");
  //   }
  //   printf("\n");
  // }

  for(int i=0; i<n; i++)
  {
    double dtfm = dtf / mass[type[i]];
    v[i][0] += dtfm * f[i][0];
    v[i][1] += dtfm * f[i][1];
    v[i][2] += dtfm * f[i][2];
  }

  // vnorm1 = 0.0;
  // vnorm2 = 0.0; 
  // for(int i=0; i<atom->nlocal; i++)
  // {
  //   for(int j=0; j<3; j++)
  //   {
  //     vnorm1 += atom->v[i][j];
  //     vnorm2 += atom->v[i][j] * atom->v[i][j];
  //   }
  // }

  // printf("end b_step, iworld = %d, v:\n", universe->iworld);
  // printf("vnorm1: %.16e\n", vnorm1);
  // printf("vnorm2: %.16e\n", vnorm2);
  // if(universe->iworld==0) 
  // {
  //   printf("end b_step, v:\n");
  //   for(int i=0; i<atom->nlocal; i++)
  //   {
  //     printf("%ld  ", atom->tag[i]);
  //     for(int j=0; j<3; j++)
  //     {
  //       printf("%.8e  ", atom->v[i][j]);
  //     }
  //     printf("\n");
  //   }
  //   printf("\n");
  // }

  // if(removecomflag) remove_com_motion();
  // double vnorm1 = 0.0, vnorm2 = 0.0; 
  // for(int i=0; i<atom->nlocal; i++)
  // {
  //   for(int j=0; j<3; j++)
  //   {
  //     vnorm1 += atom->v[i][j];
  //     vnorm2 += atom->v[i][j] * atom->v[i][j];
  //   }
  // }

  // printf("end b_step, v:\n");
  // printf("vnorm1: %.16e\n", vnorm1);
  // printf("vnorm2: %.16e\n", vnorm2);

}

/* ---------------------------------------------------------------------- */

void FixDPPimd::v_press_step(){
  // printf("vw = %.30e\nfactor = %.30e\n", vw, vw * (1 + 1. / atom->natoms));
  int nlocal = atom->nlocal;
  double **v = atom->v;
  // if(universe->iworld == 0)
  // {
    // double expv = exp(-0.25 * dtv * vw * (1 + 1. / atom->natoms / np));
    double expv = exp(-0.5 * dtv * vw * (1 + 1. / atom->natoms / np));
    for(int i=0; i<nlocal; i++)
    {
      for(int j=0; j<3; j++)
      {
        // v[i][j] = exp(-dtv * vw * (1 + 1. / atom->natoms)) * v[i][j];
        v[i][j] = expv * v[i][j];
        // v[i][j] = expv * v[i][j];
      } 
    }       
  // }
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::x_press_step(){
  // if(universe->iworld == 0)
  // {
    int nlocal = atom->nlocal;
    tagint *tag = atom->tag;
    double **x = atom->x;
    imageint *image = atom->image;
    double expq = exp(0.5 * dtv * vw);
    // if(mapflag)
    // {
    //   for(int i=0; i<nlocal; i++)
    //   {
    //     domain->unmap_inv(x[i], image[i]);
    //   }
    // }
    for(int i=0; i<nlocal; i++)
    {
      for(int j=0; j<3; j++)
      {
        x[i][j] = expq * x[i][j];
      } 
    }       
    // if(mapflag){
    //   for(int i=0; i<nlocal; i++)
    //   {
    //     domain->unmap(x[i], image[i]);
    //   }
    // }
  // }
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::press_remap(){
    double expq = exp(0.5 * dtv * vw);
    int nlocal = atom->nlocal;
    // if(universe->iworld==0) printf("\nin qc_step, expq = %.15e\n\n", expq);
    //double expv = exp(-(1. + 1./atom->natoms) * dtv * vw);
    // double expv = exp(-dtv * vw);
    if(universe->iworld == 0)
    {
      // domain->x2lamda(nlocal);
      // if(barostat == BZP)
      // {
      //   for(int i=0; i<nlocal; i++)
      //   {
      //     for(int j=0; j<3; j++)
      //     {
      //       x[i][j] = expq * x[i][j] + (expq - expv) / 2. / vw * v[i][j];
      //       v[i][j] = expv * v[i][j];
      //     } 
      //   }
      // }
      // else if(barostat == MTTK)
      // {

        //     // x[i][j] = expq * x[i][j] + (expq - expv) / 2. / vw * v[i][j];
        //     // v[i][j] = exp(-dtv * vw * (1 + 1. / atom->natoms)) * v[i][j];

      // }

      // printf("in qc_step, v:\n");
      // for(int i=0; i<nlocal; i++)
      // {
      //   printf("%ld  ", tag[i]);
      //   for(int j=0; j<3; j++)
      //   {
      //     printf("%.8e  ", v[i][j]);
      //   }
      //   printf("\n");
      // }
      // printf("\n");

    }
    domain->xprd *= expq;
    domain->yprd *= expq;
    domain->zprd *= expq;

    domain->boxlo[0] = -0.5*domain->xprd;
    domain->boxlo[1] = -0.5*domain->yprd;
    domain->boxlo[2] = -0.5*domain->zprd;
    domain->boxhi[0] = 0.5*domain->xprd;
    domain->boxhi[1] = 0.5*domain->yprd;
    domain->boxhi[2] = 0.5*domain->zprd;

    domain->set_global_box();
    domain->set_local_box();

    if(universe->iworld == 0)
    {
      // domain->lamda2x(nlocal);
    }

}

/* ---------------------------------------------------------------------- */

void FixDPPimd::qc_step(){
  // if(universe->iworld==0) printf("\nstart qc_step, vol = %.30e\n h = (%.8e %.8e %.8e)\n", domain->xprd*domain->yprd*domain->zprd, domain->xprd, domain->yprd, domain->zprd);
  int nlocal = atom->nlocal;
  double **x = atom->x;
  double **v = atom->v;
  tagint *tag = atom->tag;

  // if(universe->iworld==0) 
  // {
  // printf("start qc_step, x:\n");
  // for(int i=0; i<nlocal; i++)
  // {
  //   printf("%ld  ", tag[i]);
  //   for(int j=0; j<3; j++)
  //   {
  //     printf("%.8e  ", x[i][j]);
  //   }
  //   printf("\n");
  // }
  // printf("\n");
  // }

  if(!pextflag) {
    if(universe->iworld == 0)
    {

      //fprintf(stdout, "executing qc_step, iworld=%ld.\n", universe->iworld);
      for(int i=0; i<nlocal; i++)
      {
        x[i][0] += dtv * v[i][0];
        x[i][1] += dtv * v[i][1];
        x[i][2] += dtv * v[i][2];
      }
      //fprintf(stdout, "iworld=%lld, x=%.6f.\n", universe->iworld, x[0][0]);
    }
  }
  else{
    if(universe->iworld == 0)
    {
      // printf("vw = %.30e\n", vw);
      double expq = exp(dtv * vw);
      double expp = exp(-dtv * vw);
      if(barostat == BZP)
      {
        for(int i=0; i<nlocal; i++)
        {
          for(int j=0; j<3; j++)
          {
            x[i][j] = expq * x[i][j] + (expq - expp) / 2. / vw * v[i][j];
            v[i][j] = expp * v[i][j];
          } 
        }
        domain->xprd *= expq;
        domain->yprd *= expq;
        domain->zprd *= expq;

      }
    }    

      MPI_Barrier(universe->uworld);
      MPI_Bcast(&domain->xprd, 1, MPI_DOUBLE, 0, universe->uworld);
      MPI_Bcast(&domain->yprd, 1, MPI_DOUBLE, 0, universe->uworld);
      MPI_Bcast(&domain->zprd, 1, MPI_DOUBLE, 0, universe->uworld);

      domain->boxlo[0] = -0.5*domain->xprd;
      domain->boxlo[1] = -0.5*domain->yprd;
      domain->boxlo[2] = -0.5*domain->zprd;
      domain->boxhi[0] = 0.5*domain->xprd;
      domain->boxhi[1] = 0.5*domain->yprd;
      domain->boxhi[2] = 0.5*domain->zprd;

      domain->set_global_box();
      domain->set_local_box();
  }

  // if(universe->iworld==0) printf("\nend qc_step, vol = %.30e\n h = (%.8e %.8e %.8e)\n", domain->xprd*domain->yprd*domain->zprd, domain->xprd, domain->yprd, domain->zprd);
  // printf("\niworld = %d, end qc_step, vol = %.30e\n h = (%.8e %.8e %.8e)\n", universe->iworld, domain->xprd*domain->yprd*domain->zprd, domain->xprd, domain->yprd, domain->zprd);
  // if(universe->iworld==0) printf("end qc_step, vol = %.8e h = (%.8e %.8e %.8e)\n\n", domain->xprd*domain->yprd*domain->zprd, domain->xprd, domain->yprd, domain->zprd);
  // if(universe->iworld==0) printf("end qc_step, x:\n");
  // if(universe->iworld==0) {
  // for(int i=0; i<nlocal; i++)
  // {
    // printf("%ld  ", tag[i]);
    // for(int j=0; j<3; j++)
    // {
      // printf("%.8e  ", x[i][j]);
  //   }
  //   printf("\n");
  // }
  // printf("\n");
  // }

  // if(universe->iworld == 0)
  // {
  //   double vnorm1 = 0.0, vnorm2 = 0.0; 
  //   for(int i=0; i<atom->nlocal; i++)
  //   {
  //     for(int j=0; j<3; j++)
  //     {
  //       vnorm1 += atom->v[i][j];
  //       vnorm2 += atom->v[i][j] * atom->v[i][j];
  //     }
  //   }

  //   printf("end qc_step, v:\n");
  //   printf("vnorm1: %.16e\n", vnorm1);
  //   printf("vnorm2: %.16e\n", vnorm2);

  //   double xnorm1 = 0.0, xnorm2 = 0.0; 
  //   for(int i=0; i<atom->nlocal; i++)
  //   {
  //     for(int j=0; j<3; j++)
  //     {
  //       xnorm1 += atom->v[i][j];
  //       xnorm2 += atom->v[i][j] * atom->v[i][j];
  //     }
  //   }

  //   printf("end qc_step, x:\n");
  //   printf("xnorm1: %.16e\n", xnorm1);
  //   printf("xnorm2: %.16e\n", xnorm2);
  // }
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::a_step(){
  int n = atom->nlocal;
  double **x = atom->x;
  double **v = atom->v;
  double x0, x1, x2, v0, v1, v2; // three components of x[i] and v[i]

  if(universe->iworld != 0)
  {
    for(int i=0; i<n; i++)
    {
      x0 = x[i][0]; x1 = x[i][1]; x2 = x[i][2];
      v0 = v[i][0]; v1 = v[i][1]; v2 = v[i][2];
      x[i][0] = Lan_c[universe->iworld] * x0 + 1./_omega_k[universe->iworld] * Lan_s[universe->iworld] * v0;
      x[i][1] = Lan_c[universe->iworld] * x1 + 1./_omega_k[universe->iworld] * Lan_s[universe->iworld] * v1;
      x[i][2] = Lan_c[universe->iworld] * x2 + 1./_omega_k[universe->iworld] * Lan_s[universe->iworld] * v2;
      v[i][0] = -1.*_omega_k[universe->iworld] * Lan_s[universe->iworld] * x0 + Lan_c[universe->iworld] * v0;
      v[i][1] = -1.*_omega_k[universe->iworld] * Lan_s[universe->iworld] * x1 + Lan_c[universe->iworld] * v1;
      v[i][2] = -1.*_omega_k[universe->iworld] * Lan_s[universe->iworld] * x2 + Lan_c[universe->iworld] * v2;
    }
  }

}

/* ---------------------------------------------------------------------- */

void FixDPPimd::remove_com_motion(){
  if(universe->iworld == 0)
  {
  // double **x = atom->x;
    double **v = atom->v;
  int *mask = atom->mask;
    int nlocal = atom->nlocal;
    if (dynamic)  masstotal = group->mass(igroup);
    double vcm[3];
    group->vcm(igroup,masstotal,vcm);    
    for (int i = 0; i < nlocal; i++) {
      if (mask[i] & groupbit) {
        v[i][0] -= vcm[0];
        v[i][1] -= vcm[1];
        v[i][2] -= vcm[2];
      }
    }
  }
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::svr_step(MPI_Comm which)
{
  int nlocal = atom->nlocal;
  int *type = atom->type;
  double beta_np = 1.0 / force->boltz / Lan_temp / np * force->mvv2e;

  // compute bead kinetic energy
  double ke_0 = 0.0, ke_total = 0.0;
  for(int i=0; i<nlocal; i++) for(int j=0; j<3; j++) ke_0 += 0.5 * mass[type[i]] * atom->v[i][j] * atom->v[i][j];
  MPI_Allreduce(&ke_0, &ke_total, 1, MPI_DOUBLE, MPI_SUM, which);

  // compute alpha
  double noise_ = 0.0, noise_total = 0.0, ksi0_ = 0.0, ksi_ = 0.0;
  for(int i=0; i<atom->natoms; i++) 
  {
    for(int j=0; j<3; j++) 
    {
      ksi_ = random->gaussian();
      if(i==0 && j==0 && universe->iworld==0) ksi0_ = ksi_;
      noise_ += ksi_ * ksi_;
    }
  }
  MPI_Allreduce(&noise_, &noise_total, 1, MPI_DOUBLE, MPI_SUM, which);
  //MPI_Bcast(&ksi0_, 1, MPI_DOUBLE, 0, which);
  
  if(universe->me == 0)
  {
    alpha2 = c1 + (1.0 - c1) * (noise_total) / 2 / beta_np / ke_total + 2 * ksi0_ * sqrt(c1 * (1.0 - c1) / 2 / beta_np / ke_total);
    sgn_ = ksi0_ + sqrt(2 * beta_np * ke_total * c1 / (1.0 - c1));
    // sgn = sgn_ / abs(sgn_);
    if(sgn_<0) sgn = -1.0;
    else sgn = 1.0;
    alpha = sgn * sqrt(alpha2);
  }
  //fprintf(stdout, "iworld = %d, ke_total = %.6e, ksi0_ = %.6e, noise_total = %.6e, c1 = %.6e, alpha2 = %.6e.\n", universe->iworld, ke_total, ksi0_, noise_total, c1, alpha2);

  // broadcast alpha to the other processes in this world world
  MPI_Bcast(&alpha, 1, MPI_DOUBLE, 0, which);

  //fprintf(stdout, "iworld = %d, me = %d, alpha = %.6e.\n", universe->iworld, universe->me, alpha);

  // scale the velocities
  for(int i=0; i<nlocal; i++)
  {
    for(int j=0; j<3; j++)
    {
      atom->v[i][j] *= alpha;
    }
  }

}

/* ---------------------------------------------------------------------- */

void FixDPPimd::press_v_step()
{
  // if(universe->iworld==0) printf("iworld = %d, start press_v_step, pw = %.8e.\n", universe->iworld, vw*W);
  // printf("me = %d iworld = %d, start, vw = %.30e.\n", universe->me, universe->iworld, vw);
  // printf("me = %d iworld = %d, start, p_cv = %.30e.\n", universe->me, universe->iworld, p_cv);
  int nlocal = atom->nlocal;
  double **f = atom->f;
  double **v = atom->v;
  int *type = atom->type; 
  volume = domain->xprd * domain->yprd * domain->zprd;

  if(barostat == BZP)
  {
    vw += dtv * 3 * (volume * np * (p_cv - Pext) / force->nktv2p + Vcoeff / beta_np) / W;
    // printf("iworld = %d, p_cv = %.6e, Pext = %.6e, beta_np = %.6e, W = %.6e.\n", universe->iworld, np*p_cv, Pext, beta_np, W);
    // printf("me = %d iworld = %d, after press, vw = %.30e.\n", universe->me, universe->iworld, vw);
    // if(universe->iworld==0) printf("iworld = %d, after adding kinetic part, pw = %.30e.\n", universe->iworld, vw*W);
    if(universe->iworld==0)
    {
      double dvw_proc = 0.0, dvw = 0.0;
      for(int i = 0; i < nlocal; i++)
      {
        for(int j = 0; j < 3; j++)
        {
          dvw_proc += dtv2 * f[i][j] * v[i][j] / W + dtv3 * f[i][j] * f[i][j] / mass[type[i]] / W;
        }
      }
      // printf("me = %d iworld = %d, dvw_proc = %.30e.\n", universe->me, universe->iworld, dvw_proc);
      MPI_Allreduce(&dvw_proc, &dvw, 1, MPI_DOUBLE, MPI_SUM, world);
      // printf("me = %d iworld = %d, after reducing dvw = %.30e.\n", universe->me, universe->iworld, dvw);
      // MPI_Allreduce(&dvw_proc, &dvw, 1, MPI_DOUBLE, MPI_SUM, world);
      vw += dvw;
      // printf("me = %d iworld = %d, before broadcasting vw = %.30e.\n", universe->me, universe->iworld, vw);
      // if(universe->iworld==0) printf("iworld = %d, after adding force part, pw = %.30e.\n", universe->iworld, vw*W);
    }
    MPI_Barrier(universe->uworld);
    MPI_Bcast(&vw, 1, MPI_DOUBLE, 0, universe->uworld);
    // printf("iworld = %d, after broadcasting, pw = %.30e.\n", universe->iworld, vw*W);
    // printf("me = %d iworld = %d, after broadcasting, vw = %.30e.\n", universe->me, universe->iworld, vw);
  }
  else if(barostat == MTTK)
  {
    // if(universe->iworld==0)
    // {
      // printf("start vw = %.30e\n", vw);
      // mtk_term1 = 1. / atom->natoms * tdof * t_current / 3;
      // mtk_term1 = 2. / atom->natoms * ke_bead / 3;
      mtk_term1 = 2. / atom->natoms * totke / 3;
      // f_omega = (volume * np * (p_cv - Pext) + mtk_term1) / W;
      f_omega = (volume * np * (p_md - Pext) + mtk_term1) / W;
      // f_omega = volume * (p_cv - Pext) / W + mtk_term1 / W;
      vw += 0.5 * dtv * f_omega;
      // printf("p_current = %.30e p_hydro = %.30e vol = %.30e \nW = %.30e mtk_term1 = %.30e \nf_omega = %.30e ", p_cv, Pext, volume, W, mtk_term1, f_omega);
      // printf("vw = %.30e\n", vw);
    // }
    // MPI_Bcast(&vw, 1, MPI_DOUBLE, 0, universe->uworld);
  }
  // printf("iworld = %d, ending press_v_step, pw = %.8e.\n", universe->iworld, vw*W);
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::press_o_step()
{
  if(universe->me==0)
  {
    r1 = random->gaussian();
  // r1 = 0.0;
    vw = c1 * vw + c2 * sqrt(1. / W / beta_np) * r1;
  }
  MPI_Barrier(universe->uworld);
  MPI_Bcast(&vw, 1, MPI_DOUBLE, 0, universe->uworld);
}

void FixDPPimd::o_step()
{
  int nlocal = atom->nlocal;
  int *type = atom->type;
  double beta_np = 1.0 / force->boltz / Lan_temp / np * force->mvv2e;
  if(thermostat == PILE_L)
  {
    for(int i=0; i<nlocal; i++)
    {
      r1 = random->gaussian();
      r2 = random->gaussian();
      r3 = random->gaussian();
      // r1 = r2 = r3 = 0.0;
      //char* rns;
      //sprintf(rns, "%.6e %.6e %.6e\n", r1, r2, r3); 
      //fwrite(rns, sizeof(char), sizeof(rns), frand);
      //fprintf(frand, "%ld %d %.6e %.6e %.6e\n", update->ntimestep, i, r1, r2, r3);
      atom->v[i][0] = c1_k[universe->iworld] * atom->v[i][0] + c2_k[universe->iworld] * sqrt(1.0 / mass[type[i]] / beta_np) * r1; 
      atom->v[i][1] = c1_k[universe->iworld] * atom->v[i][1] + c2_k[universe->iworld] * sqrt(1.0 / mass[type[i]] / beta_np) * r2;
      atom->v[i][2] = c1_k[universe->iworld] * atom->v[i][2] + c2_k[universe->iworld] * sqrt(1.0 / mass[type[i]] / beta_np) * r3;
    }
      //fprintf(stdout, "iworld = %d, after o, v = %.8e.\n", universe->iworld, atom->v[0][0]);
  }
  else if(thermostat == SVR)
  {
    svr_step(universe->uworld);
  }
  else if(thermostat == PILE_G)
  {
    if(universe->iworld == 0)
    {
      svr_step(world);
    }
    else
    {
      for(int i=0; i<nlocal; i++)
      {
        r1 = random->gaussian();
        r2 = random->gaussian();
        r3 = random->gaussian();
        atom->v[i][0] = c1_k[universe->iworld] * atom->v[i][0] + c2_k[universe->iworld] * sqrt(1.0 / mass[type[i]] / beta_np) * r1; 
        atom->v[i][1] = c1_k[universe->iworld] * atom->v[i][1] + c2_k[universe->iworld] * sqrt(1.0 / mass[type[i]] / beta_np) * r2;
        atom->v[i][2] = c1_k[universe->iworld] * atom->v[i][2] + c2_k[universe->iworld] * sqrt(1.0 / mass[type[i]] / beta_np) * r3;
      }
    }
  }
}

/* ----------------------------------------------------------------------
   Normal Mode PIMD
------------------------------------------------------------------------- */

void FixDPPimd::nmpimd_init()
{
  memory->create(M_x2xp, np, np, "fix_feynman:M_x2xp");
  memory->create(M_xp2x, np, np, "fix_feynman:M_xp2x");

  lam = (double*) memory->smalloc(sizeof(double)*np, "FixDPPimd::lam");

  // Set up  eigenvalues

  // lam[0] = 0.0;
  // if(np%2==0) lam[np-1] = 4.0;
  for(int i=0; i<np; i++){
    double sin_tmp = sin(i * MY_PI / np);
    lam[i] = 4 * sin_tmp * sin_tmp;
  }

  // for(int i=2; i<=np/2; i++)
  // {
    // lam[2*i-3] = lam[2*i-2] = 2.0 * (1.0 - 1.0 *cos(2.0*MY_PI*(i-1)/np));
  // }

  // Set up eigenvectors for degenerated modes
  for(int j=0; j<np; j++){
    // for(int i=0; i<(np-1)/2; i++) for(int j=0; j<np; j++)
    for(int i=1; i<int(np/2) + 1; i++) 
    {
      printf("i = %d\n", i);
      M_x2xp[i][j] =   sqrt(2.0) * cos ( 2.0 * MY_PI * double(i) * double(j) / double(np)) / sqrt(np);
      // M_x2xp[2*i+1][j] =   sqrt(2.0) * cos ( 2.0 * MY_PI * (i+1) * j / np) / sqrt(np);
      // M_x2xp[2*i+2][j] = - sqrt(2.0) * sin ( 2.0 * MY_PI * (i+1) * j / np) / sqrt(np);
    }
    for(int i=int(np/2)+1; i<np; i++)
    {
      printf("i = %d\n", i);
      M_x2xp[i][j] =   sqrt(2.0) * sin ( 2.0 * MY_PI * double(i) * double(j) / double(np)) / sqrt(np);
    }
  }

  // Set up eigenvectors for non-degenerated modes
  for(int i=0; i<np; i++)
  {
    M_x2xp[0][i] = 1.0 / sqrt(np);
    // if(np%2==0) M_x2xp[np-1][i] = 1.0 / sqrt(np) * pow(-1.0, i);
    if(np%2==0) M_x2xp[np/2][i] = 1.0 / sqrt(np) * pow(-1.0, i);
    printf("i = %d %.8e %.8e\n", i, M_x2xp[0][i], M_x2xp[np/2][i]);
  }

  // Set up Ut

  for(int i=0; i<np; i++)
    for(int j=0; j<np; j++)
    {
      M_xp2x[i][j] = M_x2xp[j][i];
    }

  printf("eigenvalues:\n");
  for(int i=0; i<np; i++) printf("%.8e\n", lam[i]);
  printf("Mx2xp:\n");
  for(int i=0; i<np; i++){
    for(int j=0; j<np; j++){
      printf("%.8e  ", M_x2xp[i][j]);
    }
    printf("\n");
  }
  printf("\nMxp2x:\n");
  for(int i=0; i<np; i++){
    for(int j=0; j<np; j++){
      printf("%.8e  ", M_xp2x[i][j]);
    }
    printf("\n");
  }

  // Set up masses

  int iworld = universe->iworld;

  for(int i=1; i<=atom->ntypes; i++)
  {
    mass[i] = atom->mass[i];
    printf("set m = %.4e\n", mass[i]);

    if(iworld)
    {
      if(fmmode==physical) { mass[i] *= 1.0; }
      else if(fmmode==normal) { mass[i] *= lam[iworld]; }
//      mass[i] *= lam[iworld];
      mass[i] *= fmass;
    }
    printf("eig mass = %.4e\n", mass[i]);
  }
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::nmpimd_fill(double **ptr)
{
  comm_ptr = ptr;
  comm->forward_comm_fix(this);
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::nmpimd_transform(double** src, double** des, double *vector)
{
  int n = atom->nlocal;
  int m = 0;

  //fprintf(stdout, "starting, src=%.6f %.6f, des=%.6f %.6f, vec=%.6f %.6f\n", src[0][0], src[1][0], des[0][0], des[1][0], vector[0], vector[1]);

  for(int i=0; i<n; i++) for(int d=0; d<3; d++)
  {
    des[i][d] = 0.0;
    for(int j=0; j<np; j++) { des[i][d] += (src[j][m] * vector[j]); }
    m++;
  }
  //fprintf(stdout, "ending, src=%.6f %.6f, des=%.6f %.6f, vec=%.6f %.6f\n", src[0][0], src[1][0], des[0][0], des[1][0], vector[0], vector[1]);
}

/* ----------------------------------------------------------------------
   Comm operations
------------------------------------------------------------------------- */

void FixDPPimd::comm_init()
{
  if(size_plan)
  {
    delete [] plan_send;
    delete [] plan_recv;
  }

  /*
  if(method == PIMD)
  {
    size_plan = 2;
    plan_send = new int [2];
    plan_recv = new int [2];
    mode_index = new int [2];

    int rank_last = universe->me - comm->nprocs;
    int rank_next = universe->me + comm->nprocs;
    if(rank_last<0) rank_last += universe->nprocs;
    if(rank_next>=universe->nprocs) rank_next -= universe->nprocs;

    plan_send[0] = rank_next; plan_send[1] = rank_last;
    plan_recv[0] = rank_last; plan_recv[1] = rank_next;

    mode_index[0] = 0; mode_index[1] = 1;
    x_last = 1; x_next = 0;
  }
  else
  {    
    */
    int ncomms = comm->nprocs;
    size_plan = universe->nprocs - ncomms;
    plan_send = new int [size_plan];
    plan_recv = new int [size_plan];
    mode_index = new int [size_plan];

    for(int i=0; i<np-1; i++)
    {
      int i_send = (universe->iworld + i + 1) % np;
      int i_recv = (universe->iworld - i - 1 + np) % np;
      for(int j=0; j<ncomms; j++)
      {
        plan_send[i*ncomms+j] = i_send*ncomms + (comm->me+j)%ncomms;
        plan_recv[i*ncomms+j] = i_recv*ncomms + (comm->me-j+ncomms)%ncomms;
        mode_index[i*ncomms+j] = i_send;
      }
      // plan_send[i] = universe->me + comm->nprocs * (i+1);
      // if(plan_send[i]>=universe->nprocs) plan_send[i] -= universe->nprocs;

      // plan_recv[i] = universe->me - comm->nprocs * (i+1);
      // if(plan_recv[i]<0) plan_recv[i] += universe->nprocs;

    }
    // printf("me = %d\n", universe->me);
    // for(int i=0; i<size_plan; i++) printf("%d %d\n", plan_send[i], plan_recv[i]);
    // printf("\n");

    // x_next = (universe->iworld+1+universe->nworlds)%(universe->nworlds);
    // x_last = (universe->iworld-1+universe->nworlds)%(universe->nworlds);
  // }

  if(buf_beads)
  {
    for(int i=0; i<np; i++) if(buf_beads[i]) delete [] buf_beads[i];
    delete [] buf_beads;
  }

  buf_beads = new double* [np];
  for(int i=0; i<np; i++) buf_beads[i] = nullptr;
  
  if(coords)
  {
    for(int i=0; i<np; i++) if(coords[i]) delete [] coords[i];
    delete [] coords;
  }

  if(forces)
  {
    for(int i=0; i<np; i++) if(forces[i]) delete [] forces[i];
    delete [] forces;
  }
  
  coords = new double* [np];
  for(int i=0; i<np; i++) coords[i] = nullptr;

  forces = new double* [np];
  for(int i=0; i<np; i++) forces[i] = nullptr;
  
  if(x_scaled)
  {
    for(int i=0; i<np; i++) if(x_scaled[i]) delete [] x_scaled[i];
    delete [] x_scaled;
  }

  x_scaled = new double* [np];
  for(int i=0; i<np; i++) x_scaled[i] = nullptr;

  printf("trying to allocate!\n");
  int nlocal = atom->nlocal;
  max_nlocal = nlocal+300;
  max_nsend = nlocal+300;
  int size = sizeof(double) * max_nlocal * 3;

  buf_beads = new double*[np];
  for(int i=0; i<np; i++)
  {
    buf_beads[i] = (double*) memory->smalloc(size, "FixDPPimd:buf_beads[i]");
    // buf_beads[i] = new double[max_nlocal];
  }

  buf_send = (double*) memory->smalloc(sizeof(double)*max_nlocal*3, "FixDPPimd:buf_send");
  buf_recv = (double*) memory->smalloc(sizeof(double)*max_nlocal*3, "FixDPPimd:buf_recv");

  tag_search = (tagint*) memory->smalloc(sizeof(tagint)*max_nsend, "FixDPPimd:tag_search");
  tag_send = (tagint*) memory->smalloc(sizeof(tagint)*max_nsend, "FixDPPimd:tag_send");    
  tag_recv = (tagint*) memory->smalloc(sizeof(tagint)*max_nsend, "FixDPPimd:tag_recv");    

  printf("me = %d, comm allocated!\n", universe->me);

}

/* ---------------------------------------------------------------------- */

void FixDPPimd::comm_exec(double **ptr)
{
  // printf("me = %d, start comm_exec, m = %.4e\n", universe->me, mass[1]);
  // printf("me = %d, start comm_exec\n", universe->me);
  int nlocal = atom->nlocal;

  if(nlocal > max_nlocal)
  {
    max_nlocal = nlocal+200;
    int size = sizeof(double) * max_nlocal * 3;

    // printf("let's print buf_beads!\n");
    for(int i=0; i<np; i++)
    // {
    //   for(int j=0; j<nlocal; j++)
    //   {
    //     printf("%.4e %.4e %.4e\n", buf_beads[i][3*j], buf_beads[i][3*j+1], buf_beads[i][3*j+2]);
    //   }
    // }
    buf_beads[i] = (double*) memory->srealloc(buf_beads[i], size, "FixDPPimd:buf_beads[i]");
  }

  // printf("copying! m = %.4e\n", mass[1]);
  // copy the local positions
  memcpy(&(buf_beads[universe->iworld][0]), &(ptr[0][0]), sizeof(double)*nlocal*3);
  // printf("copyed! m = %.4e\n", mass[1]);

  // printf("me = %d, going over comm plans\n", universe->me);
  // go over comm plans
  for(int iplan = 0; iplan<size_plan; iplan++)
  {
    // printf("me = %d, iplan = %d\n", universe->me, iplan);
    if(iplan % comm->nprocs == 0)  nfound = 0;
    // sendrecv nlocal
    nsend = 0;
    // printf("me = %d, iplan = %d, plan_send = %d, plan_recv = %d\n", universe->me, iplan, plan_send[iplan], plan_recv[iplan]);
    MPI_Sendrecv( &(nlocal), 1, MPI_INT, plan_send[iplan], 0,
                  &(nsearch),  1, MPI_INT, plan_recv[iplan], 0, universe->uworld, MPI_STATUS_IGNORE);

    // printf("me = %d, iplan = %d, n sent\n", universe->me, iplan);

    // allocate arrays
    if(nsearch > max_nsend)
    {
      printf("nsearch = %d\n", nsearch);
      max_nsend = nsearch+200;
      tag_search = (tagint*) memory->srealloc(tag_search, sizeof(tagint)*max_nsend, "FixDPPimd:tag_search");
      tag_send = (tagint*) memory->srealloc(tag_send, sizeof(tagint)*max_nsend, "FixDPPimd:tag_send");    
      buf_send = (double*) memory->srealloc(buf_send, sizeof(double)*max_nsend*3, "FixDPPimd:buf_send");
      printf("me = %d, allocated!\n", universe->me);
    }

    // send the tags of the atoms to search
    MPI_Sendrecv(
      atom->tag, nlocal, MPI_LMP_TAGINT, plan_send[iplan], 0, tag_search,  nsearch,  MPI_LMP_TAGINT, plan_recv[iplan], 0, universe->uworld, MPI_STATUS_IGNORE
                  );

    // printf("me = %d, tags sent\n", universe->me);

    // wrap positions
    // double *wrap_ptr = buf_send;
    int ncpy = sizeof(double)*3;
    for(int i=0; i<nsearch; i++)
    {
      int index = atom->map(tag_search[i]);
      if(index >= 0 && index < nlocal){
        memcpy(&(tag_send[nsend]), &(atom->tag[index]), sizeof(tagint));
        memcpy(&(buf_send[3*nsend]), &(ptr[index][0]), ncpy);
        nsend += 1;
      }
    }
    nfound += nsend;

    // snedrecv nsend
    MPI_Sendrecv(
      &(nsend), 1, MPI_LMP_TAGINT, plan_recv[iplan], 0, 
      &(nrecv), 1, MPI_LMP_TAGINT, plan_send[iplan], 0, 
      universe->uworld, MPI_STATUS_IGNORE
    );

    // printf("me = %d, iplan = %d, nsend = %d, nrecv = %d\n", universe->me, iplan, nsend, nrecv);

    // allocate tag_recv and buf_recv
    if(nrecv > nlocal)
    {
      printf("me = %d, need to reallocated rev!\n", universe->me);
      int max_nrecv = nrecv + 200;
    tag_recv = (tagint*) memory->srealloc(tag_recv, sizeof(tagint)*max_nrecv, "FixDPPimd:tag_recv");
    buf_recv = (double*) memory->srealloc(buf_recv, sizeof(double)*max_nrecv*3, "FixDPPimd:buf_recv");
    }

    // sendrecv the tags of the found atoms
    MPI_Sendrecv(
      &(tag_send[0]), nsend, MPI_LMP_TAGINT, plan_recv[iplan], 0, 
      &(tag_recv[0]), nrecv, MPI_LMP_TAGINT, plan_send[iplan], 0, 
      MPI_COMM_WORLD, MPI_STATUS_IGNORE
    );

    // sendrecv the positions of the found atoms
    MPI_Sendrecv(
      &(buf_send[0]), nsend*3,  MPI_DOUBLE, plan_recv[iplan], 0, 
      &(buf_recv[0]), nrecv*3, MPI_DOUBLE, plan_send[iplan], 0, universe->uworld, MPI_STATUS_IGNORE);

    // printf("buf_recv: %.3f %.3f %.3f\n", buf_recv[0], buf_recv[1], buf_recv[2]);

    // copy x
    for(int i=0; i<nrecv; i++)
    {
      int index = atom->map(tag_recv[i]);
      memcpy(&(buf_beads[mode_index[iplan]][index*3]), &(buf_recv[i*3]), sizeof(double)*3);
    }

    // if((iplan+1)%comm->nprocs == 0) printf("nfound = %d\n", nfound);
    // printf("me = %d, tag = %d\n", universe->me, atom->tag[0]);
    // for(int i=0; i<np; i++){
      // printf("%.4e  ", buf_beads[i][0]);
    // }
    // printf("\n");
  }

  // printf("me = %d, success comm_exec, m = %.4e\n", universe->me, mass[1]);
}

/* ---------------------------------------------------------------------- */
/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_xc()
{
  comm_exec(atom->x);
  MPI_Barrier(universe->uworld);
  int nlocal = atom->nlocal;
  xc = (double*) memory->srealloc(xc, sizeof(double) * nlocal * 3, "FixDPPimd:xc");
  for(int i=0; i<nlocal; i++)
  {
    xc[3*i] = xc[3*i+1] = xc[3*i+2] = 0.0;
    for(int j=0; j<np; j++)
    {
      xc[3*i] += buf_beads[j][3*i];
      xc[3*i+1] += buf_beads[j][3*i+1];
      xc[3*i+2] += buf_beads[j][3*i+2];
    }
    xc[3*i] /= np;
    xc[3*i+1] /= np;
    xc[3*i+2] /= np;
  } 
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_fc()
{
  int nlocal = atom->nlocal;
  fc = (double*) memory->srealloc(fc, sizeof(double) * nlocal * 3, "FixDPPimd:fc");
  for(int i=0; i<nlocal; i++)
  {
    fc[3*i] = fc[3*i+1] = fc[3*i+2] = 0.0;
    for(int j=0; j<np; j++)
    {
      fc[3*i] += forces[j][3*i];
      fc[3*i+1] += forces[j][3*i+1];
      fc[3*i+2] += forces[j][3*i+2];
    }
    // fc[3*i] /= np;
    // fc[3*i+1] /= np;
    // fc[3*i+2] /= np;
  } 
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_vir_()
{
  MPI_Barrier(universe->uworld);
  // compute_xc();
  int nlocal = atom->nlocal;
  // fprintf(flog, "\nme = %d start compute_vir, nlocal = %d\n", universe->me, nlocal);
  xf = vir_ = xcf = centroid_vir = 0.0;
  /*
  vir2 = 0.0;
  double vxx = 0.0, vyy = 0.0, vzz = 0.0;
  double **f = atom->f;
  double **x = atom->x;
    int nall = atom->nlocal + atom->nghost;
  printf("nall = %d\n", nall);
  printf("x[0][0] = %.8e\n", x[0][0]);
  printf("f[0][0] = %.8e\n", f[0][0]);

    double xnorm1 = 0.0, xnorm2 = 0.0, xnorm1a = 0.0; 
    for(int i=0; i<nall; i++)
    {
      for(int j=0; j<3; j++)
      {
        xnorm1 += atom->x[i][j];
        xnorm1a += abs(atom->x[i][j]);
        xnorm2 += atom->x[i][j] * atom->x[i][j];
      }
    }

    printf("xnorm1: %.30e\n", xnorm1);
    printf("xnorm1a: %.30e\n", xnorm1a);
    printf("xnorm2: %.30e\n", xnorm2);

    double fnorm1 = 0.0, fnorm2 = 0.0, fnorm1a = 0.0; 
    for(int i=0; i<nall; i++)
    {
      for(int j=0; j<3; j++)
      {
        fnorm1 += atom->f[i][j];
        fnorm1a += abs(atom->f[i][j]);
        fnorm2 += atom->f[i][j] * atom->f[i][j];
      }
    }

    printf("fnorm1: %.30e\n", fnorm1);
    printf("fnorm1a: %.30e\n", fnorm1a);
    printf("fnorm2: %.30e\n", fnorm2);

    for (int i = 0; i < nall; i++) {
      vir2 += f[i][0]*x[i][0];
      vxx +=f[i][0]*x[i][0]; 
      vir2 += f[i][1]*x[i][1];
      vyy +=f[i][1]*x[i][1]; 
      vir2 += f[i][2]*x[i][2];
      vzz +=f[i][2]*x[i][2]; 
    }
    printf("vxx = %.8e\n", vxx);
    printf("vyy = %.8e\n", vyy);
    printf("vzz = %.8e\n", vzz);
    */
  for(int i=0; i<nlocal; i++)
  {
    for(int j=0; j<3; j++)
    {
      xf += x_unwrap[3*i+j] * atom->f[i][j];
      xcf += (x_unwrap[3*i+j] - xc[3*i+j]) * atom->f[i][j];
      // fprintf(flog, "me = %d iworld = %d, i = %d j = %d x %.4f xu %.4f xc %.4f f %.4f\n", universe->me, universe->iworld, i, j, atom->x[i][j], x_unwrap[3*i+j], xc[3*i+j], atom->f[i][j]);
    }
  }
  // xf /= universe->procs_per_world[universe->iworld];
  // xcf /= universe->procs_per_world[universe->iworld];
  // fprintf(flog, "step = %d me = %d iworld = %d, in compute cv, xcf = %.30e\n", update->ntimestep, universe->me, universe->iworld, xcf);
  MPI_Barrier(universe->uworld);
  MPI_Allreduce(&xf, &vir_, 1, MPI_DOUBLE, MPI_SUM, universe->uworld);
  MPI_Allreduce(&xcf, &centroid_vir, 1, MPI_DOUBLE, MPI_SUM, universe->uworld);
  // fprintf(flog, "step = %d me = %d iworld = %d, after allreduce, xcf = %.30e\n", update->ntimestep, universe->me, universe->iworld, centroid_vir);
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_vir()
{
  volume = domain->xprd * domain->yprd * domain->zprd;
  c_press->compute_vector();
  virial[0] = c_press->vector[0]*volume;
  virial[4] = c_press->vector[1]*volume;
  virial[8] = c_press->vector[2]*volume;
  virial[1] = c_press->vector[3]*volume;
  virial[2] = c_press->vector[4]*volume;
  virial[5] = c_press->vector[5]*volume;
  /*
  printf("vir_xx=%.8e\n", c_press->vector[0]*volume);
  printf("vir_yy=%.8e\n", c_press->vector[1]*volume);
  printf("vir_zz=%.8e\n", c_press->vector[2]*volume);
  */
  //int nlocal = atom->nlocal;  
  //xf = vir = xcfc = centroid_vir = 0.0;
  //for(int i=0; i<nlocal; i++)
  //{
    //for(int j=0; j<3; j++)
    //{
      //xf += atom->x[i][j] * atom->f[i][j];
      //xcfc += xc[3*i+j] * fc[3*i+j];
    //}
  //}

  //MPI_Allreduce(&xcfc, &centroid_vir, 1, MPI_DOUBLE, MPI_SUM, world);
  // printf("computing vir, vir:\n");
  // for(int i=0; i<3; i++)
  // {
  //   for(int j=0; j<3; j++)
  //   {
  //     printf("%.8e  ", virial[3*i+j]);
  //   }
  //   printf("\n");
  // }
  // printf("\n");
  vir=(virial[0]+virial[4]+virial[8]);
  vir /= universe->procs_per_world[universe->iworld];
  MPI_Barrier(universe->uworld);
  MPI_Allreduce(&vir,&vir,1,MPI_DOUBLE,MPI_SUM,universe->uworld);
  //printf("iworld=%d, vir=%.4e.\n", universe->iworld, vir);
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_xscaled()
{
  int nlocal = atom->nlocal;
  for(int i=0; i<np; i++)
  {
    x_scaled[i] = (double*) memory->srealloc(x_scaled[i], sizeof(double) * nlocal * 3, "FixDPPimd:x_scaled[i]");
  }
  for(int i=0; i<np; i++)
  {
    for(int j=0; j<nlocal; j++)
    {
    x_scaled[i][3*j] = lambda * coords[i][3*j] + (1.0 - lambda) * xc[3*j];
    x_scaled[i][3*j+1] = lambda * coords[i][3*j+1] + (1.0 - lambda) * xc[3*j+1];
    x_scaled[i][3*j+2] = lambda * coords[i][3*j+2] + (1.0 - lambda) * xc[3*j+2];
    }
  }
}

/* ---------------------------------------------------------------------- */
/* ----------------------------------------------------------------------
   Compute centroid-virial kinetic energy estimator
------------------------------------------------------------------------- */

void FixDPPimd::compute_t_vir()
{
  t_vir = -0.5 / np * vir_;
  t_cv = 1.5 * atom->natoms * force->boltz * temp - 0.5 / np * centroid_vir;
}

/* ----------------------------------------------------------------------
   Compute primitive kinetic energy estimator
------------------------------------------------------------------------- */

void FixDPPimd::compute_t_prim()
{
  // fprintf(stdout, "in compute_t_prim, me = %d, N = %d, np = %d, force->boltz = %2.8f, temp = %2.8f, total_spring_energy = %2.8e.\n", universe->me, atom->natoms, np, force->boltz, temp, total_spring_energy);
  t_prim = 1.5 * atom->natoms * np * force->boltz * temp - total_spring_energy;
}

void FixDPPimd::compute_p_prim()
{
  //p_prim = atom->natoms * force->boltz * temp * inv_volume - 1.0 / 1.5 * inv_volume * total_spring_energy;
  //p_prim = atom->natoms * force->boltz * temp * inv_volume - 1.0 / 1.5 * inv_volume * total_spring_energy + 1.0 / 3 / np * inv_volume * vir;
  // printf("N = %d, np = %d, kB = %.4e, temp = %.4e, inv_vol = %.4e, se = %.4e\n", atom->natoms, np, force->boltz, temp, inv_volume, total_spring_energy);
  p_prim = atom->natoms * np * force->boltz * temp * inv_volume - 1.0 / 1.5 * inv_volume * total_spring_energy;
  p_prim *= force->nktv2p;
  // printf("compute p_prim = %.4e\n", p_prim);
}

void FixDPPimd::compute_p_cv()
{
  inv_volume = 1.0 / (domain->xprd * domain->yprd * domain->zprd);
  p_md = 2. / 3 * inv_volume * ((totke - total_spring_energy) * force->nktv2p + 0.5 * vir / np) ;
  //p_cv = 2. / 3.  * inv_volume / np * totke - 1. / 3. / np * inv_volume * centroid_vir; 
  if(universe->iworld == 0)
  {
    // t_current = c_temp->compute_scalar();
    // p_cv = c_press2->compute_scalar();
    // c_press2->addstep(update->ntimestep+1);
    // p_cv_ = 1. / 3.  * inv_volume  * (2. * ke_bead - 1. * centroid_vir + 1. * vir) / force->nktv2p / np; 
    // printf("me = %d iworld = %d, in compute_p_cv, ke_bead = %.30e\n cv = %.30e\n vir = %.30e\n", universe->me, universe->iworld, ke_bead, centroid_vir, vir);
    p_cv = 1. / 3.  * inv_volume  * ((2. * ke_bead  - 1. * centroid_vir) * force->nktv2p + 1. * vir) / np; 
    // p_cv = 1. / 3.  * inv_volume  * (2. * ke_bead + 1. * vir) / force->nktv2p / np; 
    // p_cv = 1. / 3.  * inv_volume  * (tdof * t_current - 1. * centroid_vir + 1. * vir) / force->nktv2p / np; 
    // printf("ke = %.30e\n", tdof*t_current/2);
    // printf("in compute_p_cv, p_cv = %.30e\n", p_cv);
    // fprintf(stdout, "in compute_p_cv, iworld = %d, ke_bead = %.30e, centroid_vir = %.8e, vir = %.8e, vol = %.8e, p_cv = %.8e.\n", universe->iworld, ke_bead, centroid_vir, vir, 1./inv_volume, p_cv);
  }
    // fprintf(stdout, "in compute_p_cv, iworld = %d, ke_bead = %.30e, centroid_vir = %.8e, vir = %.8e, vol = %.8e, p_cv = %.8e.\n", universe->iworld, ke_bead, centroid_vir, vir, 1./inv_volume, p_cv);
  // }
  MPI_Barrier(universe->uworld);
  MPI_Bcast(&p_cv, 1, MPI_DOUBLE, 0, universe->uworld);
  // printf("me = %d iworld = %d, in compute_p_cv, p_cv = %.30e\n\n", universe->me, universe->iworld, p_cv);
  // fprintf(stdout, "iworld = %d.\n", universe->iworld);
}

void FixDPPimd::compute_p_vir()
{
  //inv_volume = 1. / (domain->xprd * domain->yprd * domain->zprd);
  //inv_volume = 1. / vol_;
  //p_vir = 2.  / 3 * inv_volume * totke + 1. / 3 * inv_volume * vir * force->nktv2p;
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_totke()
{
  kine = 0.0;
  // totke = 0.0;
  totke = ke_bead = 0.0;
  int nlocal = atom->nlocal;
  int *type = atom->type;
  //double *_mass = atom->mass;
  for(int i=0; i<nlocal; i++)
  {
    for(int j=0; j<3; j++)
    {
      kine += 0.5 * mass[type[i]] * atom->v[i][j] * atom->v[i][j];
    }
  }
  // printf("m = %.4e m1 = %.4e v = %.4e\n", mass[type[0]], mass[1], atom->v[0][0]);
  // printf("iworld = %d kine = %.30e\n", universe->iworld, kine);
  MPI_Barrier(universe->uworld);
  MPI_Allreduce(&kine, &ke_bead, 1, MPI_DOUBLE, MPI_SUM, world);
  ke_bead *= force->mvv2e;
  // totke = ke_bead;
  MPI_Barrier(universe->uworld);
  MPI_Allreduce(&kine, &totke, 1, MPI_DOUBLE, MPI_SUM, universe->uworld);
  totke *= force->mvv2e / np;

  c_press->compute_scalar();
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_pote()
{
  pe_bead = 0.0;
  pot_energy_partition = 0.0;
  pote = 0.0;
  c_pe->compute_scalar();
  pe_bead = c_pe->scalar;
  pot_energy_partition = pe_bead / universe->procs_per_world[universe->iworld];
  MPI_Barrier(universe->uworld);
  MPI_Allreduce(&pot_energy_partition, &pote, 1, MPI_DOUBLE, MPI_SUM, universe->uworld);
  // printf("pe_bead = %.30e\n pote = %.30e\n", pe_bead, pote);
  pote /= np;
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_spring_energy()
{
  spring_energy = 0.0;
  totke = ke_bead = 0.0;

  double **x = atom->x;
  double* _mass = atom->mass;
  int* type = atom->type;
  int nlocal = atom->nlocal;

/*
  double* xlast = buf_beads[x_last];
  double* xnext = buf_beads[x_next];

  for(int i=0; i<nlocal; i++)
  {
    double delx1 = xlast[0] - x[i][0];
    double dely1 = xlast[1] - x[i][1];
    double delz1 = xlast[2] - x[i][2];
    xlast += 3;
    domain->minimum_image(delx1, dely1, delz1);

    double delx2 = xnext[0] - x[i][0];
    double dely2 = xnext[1] - x[i][1];
    double delz2 = xnext[2] - x[i][2];
    xnext += 3;
    domain->minimum_image(delx2, dely2, delz2);

    double ff = fbond * _mass[type[i]];

    double dx = delx1+delx2;
    double dy = dely1+dely2;
    double dz = delz1+delz2;

    spring_energy += -ff * (delx1*delx1+dely1*dely1+delz1*delz1+delx2*delx2+dely2*dely2+delz2*delz2);
  }
  MPI_Allreduce(&spring_energy, &total_spring_energy, 1, MPI_DOUBLE, MPI_SUM, universe->uworld);
  total_spring_energy *= 0.25;
  total_spring_energy /= np;
*/
  // printf("iworld = %d, step = %d, fbond = %.30e\n", universe->iworld, update->ntimestep, fbond);
  for(int i=0; i<nlocal; i++)
  {
    spring_energy += 0.5 * _mass[type[i]] * fbond * lam[universe->iworld] * (x[i][0]*x[i][0] + x[i][1]*x[i][1] + x[i][2]*x[i][2]); 
  }
  MPI_Barrier(universe->uworld);
  MPI_Allreduce(&spring_energy, &se_bead, 1, MPI_DOUBLE, MPI_SUM, world);
  MPI_Barrier(universe->uworld);
  MPI_Allreduce(&spring_energy, &total_spring_energy, 1, MPI_DOUBLE, MPI_SUM, universe->uworld);
  // totke *= force->mvv2e / np;
  //fprintf(stdout, "iworld=%d, _mass=%.2e, fbond=%.2e, lam=%.2e, x=%.2e, se=%.2e.\n", universe->iworld, _mass[type[0]], fbond, lam[universe->iworld], x[0][0], spring_energy);
  // MPI_Allreduce(&spring_energy, &total_spring_energy, 1, MPI_DOUBLE, MPI_SUM, universe->uworld);
  total_spring_energy /= np;
}

/* ---------------------------------------------------------------------- */

void FixDPPimd::compute_tote()
{
  // tote = totke + hope;
  tote = totke + pote + total_spring_energy;
  // printf("pote=%f.\n", pote);
  // printf("total_spring_energy=%f.\n", total_spring_energy);
  // printf("totke=%f.\n", totke);
}

void FixDPPimd::compute_totenthalpy()
{
  volume = domain->xprd * domain->yprd * domain->zprd;
  if(barostat == BZP)  totenthalpy = tote + 0.5*W*vw*vw/np + Pext * volume / force->nktv2p - Vcoeff * kBT * log(volume);
  else if(barostat == MTTK)  totenthalpy = tote + 1.5*W*vw*vw/np + Pext * (volume - vol0);
  //totenthalpy = tote + 0.5*W*vw*vw + Pext * volume ;
  //totenthalpy = tote + 0.5*W*vw*vw + Pext * vol_ ;
  //printf("vol=%f, enth=%f.\n", volume, totenthalpy);
}

/* ---------------------------------------------------------------------- */

double FixDPPimd::compute_vector(int n)
{
  // if(n==0) { return totke; }
  if(n==0) { return ke_bead; }
  // if(n==1) { return totke; }
  if(n==1) { return se_bead; }
  // if(n==3) { return total_spring_energy; }
  //if(n==1) { return atom->v[0][0]; }
  if(n==2) { return pe_bead; }
  // if(n==5) { return pote; }
  //if(n==2) { return atom->x[0][0]; }
  //if(n==3) { if(!pextflag) {return tote;} else {return totenthalpy;} }
  //if(n==3) { return totenthalpy; }
  if(n==3) { return tote; }
  if(n==4) { return t_prim; }
  if(n==5) { return t_vir; }
  if(n==6) { return t_cv; }
  //if(n==4) { printf("returning vol_=%f\n", vol_);  return vol_; }
  // if(n==4) { return p_md; }
  //if(n==5) { return domain->xprd; }
  //if(n==6) { return domain->yprd; }
  // if(n==5) { return vir_; }
  if(n==7) { return p_prim; }
  if(n==8) { return p_md; }
  // if(n==7) { return centroid_vir; }
  // if(n==7) { return vir_-vir; }
  // if(n==7) { return p_cv - p_cv_; }
  // if(n==8) { return p_vir; }
  if(n==9) { return p_cv; }
  if(n==10) { return vw; }
  if(n==11) { return 0.5*W*vw*vw; }
  //if(pextflag) size_vector = 11;
  // if(n==10) {return Pext * volume;}
  if(n==12) {return totenthalpy;}
  return 0.0;
}
