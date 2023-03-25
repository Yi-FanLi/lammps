/* -*- c++ -*- ----------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   https://www.lammps.org/, Sandia National Laboratories
   LAMMPS development team: developers@lammps.org

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#ifdef INTEGRATE_CLASS
// clang-format off
IntegrateStyle(verlet,Verlet);
// clang-format on
#else

#ifndef LMP_VERLET_H
#define LMP_VERLET_H

#include "integrate.h"

namespace LAMMPS_NS {

class Verlet : public Integrate {
 public:
  Verlet(class LAMMPS *, int, char **);
  void init() override;
  void setup(int flag) override;
  void setup_minimal(int) override;
  void run(int) override;
  void force_clear() override;
  void cleanup() override;

  double t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, t16, t17, t18, t19, t20, t21, t22, t23, t24, t25;
  double ttot = 0.0, ttrans1 = 0.0, tcomm1 = 0.0, ttrans2 = 0.0, tcomm2 = 0.0, ttrans3 = 0.0, tcomm3 = 0.0, tvir1 = 0.0, tvir2 = 0.0, tpote = 0.0;
  double tbefore_initial = 0.0, tinitial_integrate = 0.0, tneighbor = 0.0, tforce = 0.0, tafter_force = 0.0, tpost_force = 0.0, tfinal_integrate = 0.0, tend_of_step = 0.0, tfinal = 0.0, tbefore_pair = 0.0, tpair = 0.0, tafter_pair = 0.0, tdecide = 0.0, tforward_comm = 0.0, tdomain = 0.0, texchange_and_border = 0.0, tforce_clear = 0.0, tpre_force = 0.0, tmemset = 0.0, tbefore_memset = 0.0, tafter_memset = 0.0, treturn = 0.0, tnlocal = 0.0, tnbytes = 0.0, tnghost = 0.0;
  double tpf1 = 0.0, tpf2 = 0.0;

 protected:
  int triclinic;    // 0 if domain is orthog, 1 if triclinic
  int torqueflag, extraflag;
};

}    // namespace LAMMPS_NS

#endif
#endif
