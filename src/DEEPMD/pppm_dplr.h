#ifdef KSPACE_CLASS
// clang-format off
KSpaceStyle(pppm/dplr, PPPMDPLR)
// clang-format on
#else

#ifndef LMP_PPPM_DPLR_H
#define LMP_PPPM_DPLR_H

#ifdef HIGH_PREC
#define FLOAT_PREC double
#else
#define FLOAT_PREC float
#endif

#include <iostream>
#include <vector>

#include "pppm.h"

namespace LAMMPS_NS {

class PPPMDPLR : public PPPM {
 public:
#if LAMMPS_VERSION_NUMBER < 20181109
  // See lammps/lammps#1165
  PPPMDPLR(class LAMMPS *, int, char **);
#else
  PPPMDPLR(class LAMMPS *);
#endif
  ~PPPMDPLR() override{};
  void init() override;
  const std::vector<double> &get_fele() const { return fele; };

 protected:
  void compute(int, int) override;
  void fieldforce_ik() override;
  void fieldforce_ad() override;
  double tbefore_particle_map = 0.0, tparticle_map = 0.0, tmake_rho = 0.0, treverse_comm = 0.0, tbrick2fft = 0.0, tpoisson = 0.0, tafter_poisson = 0.0, tfield_force = 0.0, tafter_field_force = 0.0, ttot = 0.0;
  double t1 = 0.0, t2 = 0.0, t3 = 0.0, t4 = 0.0, t5 = 0.0, t6 = 0.0, t7 = 0.0, t8 = 0.0, t9 = 0.0, t10 = 0.0, t11 = 0.0, t12 = 0.0;

 private:
  std::vector<double> fele;
};

}  // namespace LAMMPS_NS

#endif
#endif
