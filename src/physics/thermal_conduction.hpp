// Copyright (c) 2019-2020, Lawrence Livermore National Security, LLC and
// other Serac Project Developers. See the top-level LICENSE file for
// details.
//
// SPDX-License-Identifier: (BSD-3-Clause)

/**
 * @file thermal_solver.hpp
 *
 * @brief An object containing the solver for a thermal conduction PDE
 */

#ifndef THERMAL_CONDUCTION
#define THERMAL_CONDUCTION

#include "mfem.hpp"
#include "physics/base_physics.hpp"
#include "physics/operators/odes.hpp"
#include "physics/operators/stdfunction_operator.hpp"
#include "physics/operators/thermal_operators.hpp"

namespace serac {

/**
 * @brief An object containing the solver for a thermal conduction PDE
 *
 * This is a generic linear thermal diffusion oeprator of the form
 *
 *    M du/dt = -kappa Ku + f
 *
 *  where M is a mass matrix, K is a stiffness matrix, and f is a
 *  thermal load vector.
 */
class ThermalConduction : public BasePhysics {
public:
  /**
   * @brief A timestep method and config for the M solver
   */
  struct DynamicSolverParameters {
    TimestepMethod             timestepper;
    DirichletEnforcementMethod enforcement_method;
    LinearSolverParameters     M_params;
  };

  /**
   * @brief A configuration variant for the various solves
   * Either quasistatic, or time-dependent with timestep and M params
   */
  struct SolverParameters {
    LinearSolverParameters                 T_lin_params;
    NonlinearSolverParameters              T_nonlin_params;
    std::optional<DynamicSolverParameters> dyn_params = std::nullopt;
  };

  static IterativeSolverParameters defaultLinearParameters()
  {
    return {.rel_tol     = 1.0e-6,
            .abs_tol     = 1.0e-12,
            .print_level = 0,
            .max_iter    = 200,
            .lin_solver  = LinearSolver::CG,
            .prec        = HypreSmootherPrec{mfem::HypreSmoother::Jacobi}};
  }

  static NonlinearSolverParameters defaultNonlinearParameters()
  {
    return {.rel_tol = 1.0e-4, .abs_tol = 1.0e-8, .max_iter = 500, .print_level = 1};
  }

  static SolverParameters defaultQuasistaticParameters()
  {
    return {defaultLinearParameters(), defaultNonlinearParameters(),
            DynamicSolverParameters{TimestepMethod::QuasiStatic, DirichletEnforcementMethod::RateControl,
                                    defaultLinearParameters()}};
  }

  static SolverParameters defaultDynamicParameters()
  {
    return {defaultLinearParameters(), defaultNonlinearParameters(),
            DynamicSolverParameters{TimestepMethod::BackwardEuler, DirichletEnforcementMethod::RateControl,
                                    defaultLinearParameters()}};
  }

  /**
   * @brief Construct a new Thermal Solver object
   *
   * @param[in] order The order of the thermal field discretization
   * @param[in] mesh The MFEM parallel mesh to solve the PDE on
   * @param[in] params The system solver parameters
   */
  ThermalConduction(int order, std::shared_ptr<mfem::ParMesh> mesh, const SolverParameters& params);

  /**
   * @brief Set essential temperature boundary conditions (strongly enforced)
   *
   * @param[in] temp_bdr The boundary attributes on which to enforce a temperature
   * @param[in] temp_bdr_coef The prescribed boundary temperature
   */
  void setTemperatureBCs(const std::set<int>& temp_bdr, std::shared_ptr<mfem::Coefficient> temp_bdr_coef);

  /**
   * @brief Set flux boundary conditions (weakly enforced)
   *
   * @param[in] flux_bdr The boundary attributes on which to enforce a heat flux (weakly enforced)
   * @param[in] flux_bdr_coef The prescribed boundary heat flux
   */
  void setFluxBCs(const std::set<int>& flux_bdr, std::shared_ptr<mfem::Coefficient> flux_bdr_coef);

  /**
   * @brief Advance the timestep
   *
   * @param[inout] dt The timestep to advance. For adaptive time integration methods, the actual timestep is returned.
   */
  void advanceTimestep(double& dt) override;

  /**
   * @brief Set the thermal conductivity
   *
   * @param[in] kappa The thermal conductivity
   */
  void setConductivity(std::unique_ptr<mfem::Coefficient>&& kappa);

  /**
   * @brief Set the temperature state vector from a coefficient
   *
   * @param[in] temp The temperature coefficient
   */
  void setTemperature(mfem::Coefficient& temp);

  /**
   * @brief Set the thermal body source from a coefficient
   *
   * @param[in] source The source function coefficient
   */
  void setSource(std::unique_ptr<mfem::Coefficient>&& source);

  /**
   * @brief Get the temperature state
   *
   * @return A reference to the current temperature finite element state
   */
  const serac::FiniteElementState& temperature() const { return temperature_; };
  serac::FiniteElementState&       temperature() { return temperature_; };

  /**
   * @brief Complete the initialization and allocation of the data structures.
   *
   * This must be called before StaticSolve() or AdvanceTimestep(). If allow_dynamic
   * = false, do not allocate the mass matrix or dynamic operator
   */
  void completeSetup() override;

  /**
   * @brief Destroy the Thermal Solver object
   */
  virtual ~ThermalConduction() = default;

protected:
  /**
   * @brief The temperature finite element state
   */
  serac::FiniteElementState temperature_;

  /**
   * @brief Mass bilinear form object
   */
  std::unique_ptr<mfem::ParBilinearForm> M_form_;

  /**
   * @brief Stiffness bilinear form object
   */
  std::unique_ptr<mfem::ParBilinearForm> K_form_;

  /**
   * @brief Assembled mass matrix
   */
  std::unique_ptr<mfem::HypreParMatrix> M_;

  /**
   * @brief Assembled stiffness matrix
   */
  std::unique_ptr<mfem::HypreParMatrix> K_;

  /**
   * @brief Thermal load linear form
   */
  std::unique_ptr<mfem::ParLinearForm> l_form_;

  /**
   * @brief Assembled BC load vector
   */
  std::unique_ptr<mfem::HypreParVector> bc_rhs_;

  /**
   * @brief Assembled RHS vector
   */
  std::unique_ptr<mfem::HypreParVector> rhs_;

  /**
   * @brief Conduction coefficient
   */
  std::unique_ptr<mfem::Coefficient> kappa_;

  /**
   * @brief Body source coefficient
   */
  std::unique_ptr<mfem::Coefficient> source_;

  /**
   * @brief Configuration for dynamic equation solvers
   */
  std::optional<LinearSolverParameters> dyn_M_params_;
  std::optional<LinearSolverParameters> dyn_T_params_;

  /**
   * @brief Time integration operator
   */
  std::unique_ptr<DynamicConductionOperator> dyn_oper_;

  FirstOrderODE ode_;

  StdFunctionOperator residual;

  EquationSolver nonlin_solver_;
  EquationSolver mass_solver_;
  EquationSolver stiffness_solver_;

  double                                dt_, previous_dt_;
  std::unique_ptr<mfem::HypreParMatrix> J_;

  mfem::Vector zero_;

  mfem::Vector u_;
  mfem::Vector previous_;

  mfem::Vector U_minus_;
  mfem::Vector U_;
  mfem::Vector U_plus_;
  mfem::Vector dU_dt_;

  /**
   * @brief Solve the Quasi-static operator
   */
  void quasiStaticSolve();

  /**
   * @brief System solver instance for quasistatic K solver
   */
  std::optional<EquationSolver> K_inv_;
};

}  // namespace serac

#endif
