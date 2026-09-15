/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   https://www.lammps.org/, Sandia National Laboratories
   LAMMPS development team: developers@lammps.org

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "compute.h"
#include "fix.h"
#include "modify.h"
#include "../testing/core.h"
#include "gtest/gtest.h"

#include <mpi.h>

bool verbose = false;

namespace LAMMPS_NS {

class ComputeTempUVTTest : public LAMMPSTest {
 protected:
  void SetUp() override
  {
    LAMMPSTest::SetUp();
    if (!Info::has_package("EXTRA-FIX")) GTEST_SKIP();
    command("units lj");
    command("atom_style atomic");
    command("atom_modify map yes");
    command("region box block 0 4 0 4 0 4");
    command("create_box 1 box");
    command("create_atoms 1 single 1 1 1");
    command("create_atoms 1 single 3 3 3");
    command("mass 1 1.0");
    command("pair_style zero 0.5");
    command("pair_coeff * *");
    command("velocity all set 1 2 3");
    command("variable deriv equal 0.0");
    command("fix cp all uvt temp 1 1 0.5 mu 0 0 0.5 ne 1 ne_velocity 2 dedn v_deriv");
    command("compute nuclear all temp");
    command("compute combined all temp/uvt cp");
    command("run 0 post no");
  }
};

TEST_F(ComputeTempUVTTest, CombinedScalarAndNuclearTensor)
{
  auto *temp = lmp->modify->get_compute_by_id("combined");
  auto *nuclear = lmp->modify->get_compute_by_id("nuclear");
  auto *fix = lmp->modify->get_fix_by_id("cp");
  int dim;
  auto *ne_dot = static_cast<double *>(fix->extract("ne_dot", dim));
  auto *ne_mass = static_cast<double *>(fix->extract("ne_mass", dim));
  ASSERT_NE(ne_dot, nullptr);
  ASSERT_NE(ne_mass, nullptr);
  EXPECT_DOUBLE_EQ(*ne_mass, 0.75);
  EXPECT_DOUBLE_EQ(temp->dof, 4.0);
  // Two unit-mass atoms each have |v|^2 = 14; Ne contributes 0.75 * 2^2.
  EXPECT_DOUBLE_EQ(temp->compute_scalar(), 31.0 / 4.0);
  EXPECT_DOUBLE_EQ(temp->compute_scalar(), 31.0 / 4.0);
  *ne_dot = 4.0;
  EXPECT_DOUBLE_EQ(temp->compute_scalar(), 40.0 / 4.0);

  temp->compute_vector();
  nuclear->compute_vector();
  for (int i = 0; i < 6; ++i) EXPECT_DOUBLE_EQ(temp->vector[i], nuclear->vector[i]);
}

TEST_F(ComputeTempUVTTest, DynamicDOFAndExtraDOF)
{
  auto *temp = lmp->modify->get_compute_by_id("combined");
  command("compute_modify combined dynamic/dof yes");
  temp->setup();
  command("create_atoms 1 single 2 2 2");
  command("velocity all set 1 2 3");
  // Ne_mass stays at its prior value until FixUVT updates it.
  EXPECT_DOUBLE_EQ(temp->compute_scalar(), 45.0 / 7.0);
  EXPECT_DOUBLE_EQ(temp->dof, 7.0);
  EXPECT_DOUBLE_EQ(temp->compute_scalar(), 45.0 / 7.0);

  command("compute_modify combined extra/dof 9");
  EXPECT_DOUBLE_EQ(temp->compute_scalar(), 45.0);
  EXPECT_DOUBLE_EQ(temp->dof, 1.0);
  command("compute_modify combined extra/dof 10");
  EXPECT_ANY_THROW(temp->compute_scalar());
}

TEST_F(ComputeTempUVTTest, ValidatesFixAndGroup)
{
  EXPECT_ANY_THROW(command("compute bad all temp/uvt"));
  EXPECT_ANY_THROW(command("compute bad all temp/uvt cp extra"));
  command("compute missing all temp/uvt absent");
  EXPECT_ANY_THROW(lmp->modify->get_compute_by_id("missing")->init());
  command("fix other all nve");
  command("compute wrong all temp/uvt other");
  EXPECT_ANY_THROW(lmp->modify->get_compute_by_id("wrong")->init());
  command("group subset id 1");
  command("compute mismatch subset temp/uvt cp");
  EXPECT_ANY_THROW(lmp->modify->get_compute_by_id("mismatch")->init());

  command("unfix cp");
  EXPECT_ANY_THROW(lmp->modify->get_compute_by_id("combined")->init());
  command("fix cp all uvt temp 1 1 0.5 mu 0 0 0.5 ne 1 ne_velocity 3 dedn v_deriv");
  EXPECT_NO_THROW(lmp->modify->get_compute_by_id("combined")->init());
  command("uncompute missing");
  command("uncompute wrong");
  command("uncompute mismatch");
  command("unfix other");
  command("run 0 post no");
  EXPECT_DOUBLE_EQ(lmp->modify->get_compute_by_id("combined")->compute_scalar(), 34.75 / 4.0);
}

}    // namespace LAMMPS_NS

int main(int argc, char **argv)
{
  MPI_Init(&argc, &argv);
  ::testing::InitGoogleMock(&argc, argv);
  int result = RUN_ALL_TESTS();
  MPI_Finalize();
  return result;
}
