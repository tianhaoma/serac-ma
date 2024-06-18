// Copyright (c) 2019-2024, Lawrence Livermore National Security, LLC and
// other Serac Project Developers. See the top-level LICENSE file for
// details.
//
// SPDX-License-Identifier: (BSD-3-Clause)

/**
 * @file nonlinear_J2_material.cpp
 */

#include "serac/physics/materials/solid_material.hpp"

#include "axom/slic/core/SimpleLogger.hpp"
#include <gtest/gtest.h>

#include "serac/numerics/functional/tensor.hpp"
#include "serac/physics/materials/material_verification_tools.hpp"

namespace serac {

TEST(NonlinearJ2Material, PowerLawHardeningWorksWithDuals)
{
  double                             sigma_y = 1.0;
  solid_mechanics::PowerLawHardening hardening_law{.sigma_y = sigma_y, .n = 2.0, .eps0 = 0.01};
  double                             eqps        = 0.1;
  auto                               flow_stress = hardening_law(make_dual(eqps));
  EXPECT_GT(flow_stress.value, sigma_y);
  EXPECT_GT(flow_stress.gradient, 0.0);
};

TEST(NonlinearJ2Material, SatisfiesConsistency)
{
  tensor<double, 3, 3> du_dx{
      {{0.7551559, 0.3129729, 0.12388372}, {0.548188, 0.8851279, 0.30576992}, {0.82008433, 0.95633745, 0.3566252}}};
  solid_mechanics::PowerLawHardening hardening_law{.sigma_y = 0.1, .n = 2.0, .eps0 = 0.01};
  solid_mechanics::J2Nonlinear<solid_mechanics::PowerLawHardening> material{
      .E = 1.0, .nu = 0.25, .hardening = hardening_law, .density = 1.0};
  auto                 internal_state = solid_mechanics::J2Nonlinear<solid_mechanics::PowerLawHardening>::State{};
  tensor<double, 3, 3> stress         = material(internal_state, du_dx);
  double               mises          = std::sqrt(1.5) * norm(dev(stress));
  double               flow_stress    = hardening_law(internal_state.accumulated_plastic_strain);
  EXPECT_NEAR(mises, flow_stress, 1e-9 * mises);

  double               twoG = material.E / (1 + material.nu);
  tensor<double, 3, 3> s    = twoG * dev(sym(du_dx) - internal_state.plastic_strain);
  EXPECT_LT(norm(s - dev(stress)) / norm(s), 1e-9);
};

TEST(NonlinearJ2Material, Uniaxial)
{
  double                                            E       = 1.0;
  double                                            nu      = 0.25;
  double                                            sigma_y = 0.01;
  double                                            Hi      = E / 100.0;
  double                                            eps0    = sigma_y / Hi;
  double                                            n       = 1;
  solid_mechanics::PowerLawHardening                hardening{.sigma_y = sigma_y, .n = n, .eps0 = eps0};
  solid_mechanics::J2Nonlinear<decltype(hardening)> material{.E = E, .nu = nu, .hardening = hardening, .density = 1.0};

  auto internal_state   = solid_mechanics::J2Nonlinear<decltype(hardening)>::State{};
  auto strain           = [=](double t) { return sigma_y / E * t; };
  auto response_history = uniaxial_stress_test(2.0, 3, material, internal_state, strain);

  auto stress_exact = [=](double epsilon) {
    return epsilon < sigma_y / E ? E * epsilon : E / (E + Hi) * (sigma_y + Hi * epsilon);
  };
  auto plastic_strain_exact = [=](double epsilon) {
    return epsilon < sigma_y / E ? E * epsilon : (E * epsilon - sigma_y) / (E + Hi);
  };

  for (auto r : response_history) {
    double e  = get<1>(r)[0][0];                 // strain
    double s  = get<2>(r)[0][0];                 // stress
    double pe = get<3>(r).plastic_strain[0][0];  // plastic strain
    ASSERT_LE(std::abs(s - stress_exact(e)), 1e-10 * std::abs(stress_exact(e)));
    ASSERT_LE(std::abs(pe - plastic_strain_exact(e)), 1e-10 * std::abs(plastic_strain_exact(e)));
  }
};

TEST(FiniteDeformationNonlinearJ2Material, Uniaxial)
{
  /* Log strain J2 plasticity has the nice feature that the exact uniaxial stress solution from
     small strain plasticity are applicable, if you replace the lineasr strain with log strain
     and use the Kirchhoff stress as the output.
  */
  double                                            E       = 1.0;
  double                                            nu      = 0.25;
  double                                            sigma_y = 0.01;
  double                                            Hi      = E / 100.0;
  double                                            eps0    = sigma_y / Hi;
  double                                            n       = 1;
  solid_mechanics::PowerLawHardening                hardening{.sigma_y = sigma_y, .n = n, .eps0 = eps0};
  solid_mechanics::J2FiniteDeformationNonlinear<decltype(hardening)> material{.E = E, .nu = nu, .hardening = hardening, .density = 1.0};

  auto internal_state   = solid_mechanics::J2FiniteDeformationNonlinear<decltype(hardening)>::State{};
  auto strain           = [=](double t) { return sigma_y / E * t; };
  auto response_history = uniaxial_stress_test(2.0, 4, material, internal_state, strain);

  auto stress_exact = [=](double epsilon) {
    return epsilon < sigma_y / E ? E * epsilon : E / (E + Hi) * (sigma_y + Hi * epsilon);
  };
  auto plastic_strain_exact = [=](double epsilon) {
    return epsilon < sigma_y / E ? 0.0: (E * epsilon - sigma_y) / (E + Hi);
  };

  for (auto r : response_history) {
    double J = detApIm1(get<1>(r)) + 1;
    double e  = std::log1p(get<1>(r)[0][0]); // log strain
    double s  = get<2>(r)[0][0]*J; // Kirchhoff stress
    double pe = -std::log(get<3>(r).Fpinv[0][0]);  // plastic strain
    ASSERT_NEAR(s,  stress_exact(e), 1e-6 * std::abs(stress_exact(e)));
    ASSERT_NEAR(pe, plastic_strain_exact(e), 1e-6 * std::abs(plastic_strain_exact(e)));
  }
};

TEST(FiniteDeformationNonlinearJ2Material, DerivativeCorrectness)
{
  // This constitutive function is non-differentiable at the yield point,
  // but should be differentiable everywhere else.
  // The elastic response is trivial. We want to check the plastic reponse
  // and make sure the derivative propagates correctly through the nonlinear
  // solve.

  // parameters
  double E = 200.0e9;
  double nu = 0.25;
  double sigma_y = 350e6;
  double eps0 = sigma_y / E;
  double n = 3;

  // hardening model
  solid_mechanics::PowerLawHardening hardening{.sigma_y = sigma_y, .n = n, .eps0 = eps0};

  // material model
  solid_mechanics::J2FiniteDeformationNonlinear<decltype(hardening)> material{.E = E, .nu = nu, .hardening = hardening, .density = 1.0};

  // initialize internal state variables
  auto internal_state = solid_mechanics::J2FiniteDeformationNonlinear<decltype(hardening)>::State{};

  // clang-format off
  const tensor<double, 3, 3> H{{
    { 0.025, -0.008,  0.005},
    {-0.008, -0.01,   0.003},
    { 0.005,  0.003,  0.0}}};

  tensor< double, 3, 3 > dH = {{
    {0.3, 0.4, 1.6},
    {2.0, 0.2, 0.3},
    {0.1, 1.7, 0.3}
  }};
  // clang-format on

  auto stress_and_tangent = material(internal_state, make_dual(H));
  auto tangent = get_gradient(stress_and_tangent);

  // make sure that this load case is actually yielding
  ASSERT_GT(internal_state.accumulated_plastic_strain, 1e-3);

  const double epsilon = 1.0e-5;

  // finite difference evaluations
  auto internal_state_old_p = solid_mechanics::J2FiniteDeformationNonlinear<decltype(hardening)>::State{};
  auto stress_p = material(internal_state_old_p, H + epsilon * dH);

  auto internal_state_old_m = solid_mechanics::J2FiniteDeformationNonlinear<decltype(hardening)>::State{};
  auto stress_m = material(internal_state_old_m, H - epsilon * dH);

  // Make sure the finite difference evaluations all took the same branch (yielding).
  ASSERT_GT(internal_state_old_p.accumulated_plastic_strain, 1e-3);
  ASSERT_GT(internal_state_old_m.accumulated_plastic_strain, 1e-3);

  // check AD against finite differences
  tensor<double, 3, 3> dsig[2] = {double_dot(tangent, dH),
                                  (stress_p - stress_m) / (2 * epsilon)};

  EXPECT_LT(norm(dsig[0] - dsig[1]), 1e-5*norm(dsig[1]));
}

}  // namespace serac

int main(int argc, char* argv[])
{
  ::testing::InitGoogleTest(&argc, argv);

  axom::slic::SimpleLogger logger;

  int result = RUN_ALL_TESTS();

  return result;
}