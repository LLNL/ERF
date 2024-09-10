#include "prob.H"
#include "AMReX_Random.H"
#include <Utils/ParFunctions.H>

using namespace amrex;

std::unique_ptr<ProblemBase>
amrex_probinit(const amrex_real* problo, const amrex_real* probhi)
{
    return std::make_unique<Problem>(problo, probhi);
}

Problem::Problem(const amrex::Real* problo, const amrex::Real* probhi)
{
  // Parse params
  ParmParse pp("prob");
  pp.query("rho_0", parms.rho_0);
  pp.query("T_0", parms.T_0);
  pp.query("A_0", parms.A_0);
  pp.query("QKE_0", parms.QKE_0);

  pp.query("U_0", parms.U_0);
  pp.query("V_0", parms.V_0);
  pp.query("W_0", parms.W_0);
  pp.query("U_0_Pert_Mag", parms.U_0_Pert_Mag);
  pp.query("V_0_Pert_Mag", parms.V_0_Pert_Mag);
  pp.query("W_0_Pert_Mag", parms.W_0_Pert_Mag);
  pp.query("T_0_Pert_Mag", parms.T_0_Pert_Mag);

  pp.query("pert_deltaU", parms.pert_deltaU);
  pp.query("pert_deltaV", parms.pert_deltaV);
  pp.query("pert_periods_U", parms.pert_periods_U);
  pp.query("pert_periods_V", parms.pert_periods_V);
  pp.query("pert_ref_height", parms.pert_ref_height);
  parms.aval = parms.pert_periods_U * 2.0 * PI / (probhi[1] - problo[1]);
  parms.bval = parms.pert_periods_V * 2.0 * PI / (probhi[0] - problo[0]);
  parms.ufac = parms.pert_deltaU * std::exp(0.5) / parms.pert_ref_height;
  parms.vfac = parms.pert_deltaV * std::exp(0.5) / parms.pert_ref_height;

  pp.query("dampcoef", parms.dampcoef);
  pp.query("zdamp", parms.zdamp);

  //===========================================================================
  // READ USER-DEFINED INPUTS
  pp.get("advection_heating_rate", parms.advection_heating_rate);
  pp.query("advection_moisture_rate", parms.advection_moisture_rate);
  pp.query("restart_time",parms.restart_time);
  pp.query("source_cutoff", parms.cutoff);
  pp.query("source_cutoff_transition", parms.cutoff_transition);
  //===========================================================================

  init_base_parms(parms.rho_0, parms.T_0);
}

void
Problem::init_custom_pert(
    const amrex::Box&  bx,
    const amrex::Box& xbx,
    const amrex::Box& ybx,
    const amrex::Box& zbx,
    amrex::Array4<amrex::Real const> const& /*state*/,
    amrex::Array4<amrex::Real      > const& state_pert,
    amrex::Array4<amrex::Real      > const& x_vel_pert,
    amrex::Array4<amrex::Real      > const& y_vel_pert,
    amrex::Array4<amrex::Real      > const& z_vel_pert,
    amrex::Array4<amrex::Real      > const& /*r_hse*/,
    amrex::Array4<amrex::Real      > const& /*p_hse*/,
    amrex::Array4<amrex::Real const> const& /*z_nd*/,
    amrex::Array4<amrex::Real const> const& /*z_cc*/,
    amrex::GeometryData const& geomdata,
    amrex::Array4<amrex::Real const> const& /*mf_m*/,
    amrex::Array4<amrex::Real const> const& /*mf_u*/,
    amrex::Array4<amrex::Real const> const& /*mf_v*/,
    const SolverChoice& sc)
{
    const bool use_moisture = (sc.moisture_type != MoistureType::None);

  ParallelForRNG(bx, [=, parms_d=parms] AMREX_GPU_DEVICE(int i, int j, int k, const amrex::RandomEngine& engine) noexcept {
    // Geometry
    const Real* prob_lo = geomdata.ProbLo();
    const Real* prob_hi = geomdata.ProbHi();
    const Real* dx = geomdata.CellSize();
    const Real x = prob_lo[0] + (i + 0.5) * dx[0];
    const Real y = prob_lo[1] + (j + 0.5) * dx[1];
    const Real z = prob_lo[2] + (k + 0.5) * dx[2];

    // Define a point (xc,yc,zc) at the center of the domain
    const Real xc = 0.5 * (prob_lo[0] + prob_hi[0]);
    const Real yc = 0.5 * (prob_lo[1] + prob_hi[1]);
    const Real zc = 0.5 * (prob_lo[2] + prob_hi[2]);

    const Real r  = std::sqrt((x-xc)*(x-xc) + (y-yc)*(y-yc) + (z-zc)*(z-zc));

    // Add temperature perturbations
    if ((z <= parms_d.pert_ref_height) && (parms_d.T_0_Pert_Mag != 0.0)) {
        Real rand_double = amrex::Random(engine); // Between 0.0 and 1.0
        state_pert(i, j, k, RhoTheta_comp) = (rand_double*2.0 - 1.0)*parms_d.T_0_Pert_Mag;
    }

    // Set scalar = A_0*exp(-10r^2), where r is distance from center of domain
    state_pert(i, j, k, RhoScalar_comp) = parms_d.A_0 * exp(-10.*r*r);

    // Set an initial value for QKE
    state_pert(i, j, k, RhoQKE_comp) = parms_d.QKE_0;

    if (use_moisture) {
        state_pert(i, j, k, RhoQ1_comp) = 0.0;
        state_pert(i, j, k, RhoQ2_comp) = 0.0;
    }
  });

  // Set the x-velocity
  ParallelForRNG(xbx, [=, parms_d=parms] AMREX_GPU_DEVICE(int i, int j, int k, const amrex::RandomEngine& engine) noexcept {
    const Real* prob_lo = geomdata.ProbLo();
    const Real* dx = geomdata.CellSize();
    const Real y = prob_lo[1] + (j + 0.5) * dx[1];
    const Real z = prob_lo[2] + (k + 0.5) * dx[2];

    // Set the x-velocity
    x_vel_pert(i, j, k) = parms_d.U_0;
    if ((z <= parms_d.pert_ref_height) && (parms_d.U_0_Pert_Mag != 0.0))
    {
        Real rand_double = amrex::Random(engine); // Between 0.0 and 1.0
        Real x_vel_prime = (rand_double*2.0 - 1.0)*parms_d.U_0_Pert_Mag;
        x_vel_pert(i, j, k) += x_vel_prime;
    }
    if (parms_d.pert_deltaU != 0.0)
    {
        const amrex::Real yl = y - prob_lo[1];
        const amrex::Real zl = z / parms_d.pert_ref_height;
        const amrex::Real damp = std::exp(-0.5 * zl * zl);
        x_vel_pert(i, j, k) += parms_d.ufac * damp * z * std::cos(parms_d.aval * yl);
    }
  });

  // Set the y-velocity
  ParallelForRNG(ybx, [=, parms_d=parms] AMREX_GPU_DEVICE(int i, int j, int k, const amrex::RandomEngine& engine) noexcept {
    const Real* prob_lo = geomdata.ProbLo();
    const Real* dx = geomdata.CellSize();
    const Real x = prob_lo[0] + (i + 0.5) * dx[0];
    const Real z = prob_lo[2] + (k + 0.5) * dx[2];

    // Set the y-velocity
    y_vel_pert(i, j, k) = parms_d.V_0;
    if ((z <= parms_d.pert_ref_height) && (parms_d.V_0_Pert_Mag != 0.0))
    {
        Real rand_double = amrex::Random(engine); // Between 0.0 and 1.0
        Real y_vel_prime = (rand_double*2.0 - 1.0)*parms_d.V_0_Pert_Mag;
        y_vel_pert(i, j, k) += y_vel_prime;
    }
    if (parms_d.pert_deltaV != 0.0)
    {
        const amrex::Real xl = x - prob_lo[0];
        const amrex::Real zl = z / parms_d.pert_ref_height;
        const amrex::Real damp = std::exp(-0.5 * zl * zl);
        y_vel_pert(i, j, k) += parms_d.vfac * damp * z * std::cos(parms_d.bval * xl);
    }
  });

  // Set the z-velocity
  ParallelForRNG(zbx, [=, parms_d=parms] AMREX_GPU_DEVICE(int i, int j, int k, const amrex::RandomEngine& engine) noexcept {
    const int dom_lo_z = geomdata.Domain().smallEnd()[2];
    const int dom_hi_z = geomdata.Domain().bigEnd()[2];

    // Set the z-velocity
    if (k == dom_lo_z || k == dom_hi_z+1)
    {
        z_vel_pert(i, j, k) = 0.0;
    }
    else if (parms_d.W_0_Pert_Mag != 0.0)
    {
        Real rand_double = amrex::Random(engine); // Between 0.0 and 1.0
        Real z_vel_prime = (rand_double*2.0 - 1.0)*parms_d.W_0_Pert_Mag;
        z_vel_pert(i, j, k) = parms_d.W_0 + z_vel_prime;
    }
  });
}

//=============================================================================
// USER-DEFINED FUNCTION
//=============================================================================
void
Problem::update_rhotheta_sources (
    const amrex::Real& time,
    amrex::MultiFab* src,
    const amrex::Geometry& geom,
    std::unique_ptr<amrex::MultiFab>& z_phys_cc)
{
    if (src->empty()) return;

    const int khi              = geom.Domain().bigEnd()[2];

    // Note: If z_phys_cc, then use_terrain=1 was set. If the z coordinate
    // varies in time and or space, then the the height needs to be
    // calculated at each time step. Here, we assume that only grid
    // stretching exists.
    if (z_phys_cc && zlevels.empty()) {
        amrex::Print() << "Initializing z levels on stretched grid" << std::endl;
        zlevels.resize(khi+1);
        reduce_to_max_per_height(zlevels, z_phys_cc);
    }

    // Only apply temperature source below nominal inversion height
    for ( amrex::MFIter mfi(*src, amrex::TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        const auto &box = mfi.tilebox();
        const Array4<Real>& src_arr = src->array(mfi);
        if (box.length(0) != 1)
        {
            // if z dimension size is 1, then src is a spatially varying function over x,y at k=0
            ParallelFor(box, [=, parms_d=parms] AMREX_GPU_DEVICE (int i, int j, int k) noexcept {
                const Real* prob_lo = geom.ProbLo();
                const Real* prob_hi = geom.ProbHi();
                const Real* dx = geom.CellSize();
                const Real x = prob_lo[0] + (i + 0.5) * dx[0];
                const Real y = prob_lo[1] + (j + 0.5) * dx[1];

                // Define a point (xc,yc,zc) at the center of the domain
                const Real xc = 0.5 * (prob_lo[0] + prob_hi[0]);
                const Real yc = 0.5 * (prob_lo[1] + prob_hi[1]);

                const Real c = 2.0*dx[0];
                const Real r  = std::sqrt((x-xc)*(x-xc) + (y-yc)*(y-yc));

                src_arr(i, j, k) = parms_d.advection_heating_rate*exp(-r / (c*c));
            });
        } else {
            // src is a function over Z
            ParallelFor(box, [=, parms_d=parms] AMREX_GPU_DEVICE (int i, int j, int k) {
                src_arr(i, j, k) = parms_d.advection_heating_rate;
            });
        }
    }
}

void
Problem::update_rhoqt_sources (
    const amrex::Real& time,
    amrex::MultiFab* qsrc,
    const amrex::Geometry& geom,
    std::unique_ptr<amrex::MultiFab>& z_phys_cc)
{
    if (qsrc->empty()) return;

    const int khi              = geom.Domain().bigEnd()[2];

    // Note: If z_phys_cc, then use_terrain=1 was set. If the z coordinate
    // varies in time and or space, then the the height needs to be
    // calculated at each time step. Here, we assume that only grid
    // stretching exists.
    if (z_phys_cc && zlevels.empty()) {
        amrex::Print() << "Initializing z levels on stretched grid" << std::endl;
        zlevels.resize(khi+1);
        reduce_to_max_per_height(zlevels, z_phys_cc);
    }

    // Only apply temperature source below nominal inversion height
    for ( amrex::MFIter mfi(*qsrc, amrex::TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        const auto &box = mfi.tilebox();
        const Array4<Real>& qsrc_arr = qsrc->array(mfi);
        if (box.length(0) != 1)
        {
            // if z dimension size is 1, then src is a spatially varying function over x,y at k=0
            ParallelFor(box, [=, parms_d=parms] AMREX_GPU_DEVICE (int i, int j, int k) noexcept {
                const Real* prob_lo = geom.ProbLo();
                const Real* prob_hi = geom.ProbHi();
                const Real* dx = geom.CellSize();
                const Real x = prob_lo[0] + (i + 0.5) * dx[0];
                const Real y = prob_lo[1] + (j + 0.5) * dx[1];

                // Define a point (xc,yc,zc) at the center of the domain
                const Real xc = 0.5 * (prob_lo[0] + prob_hi[0]);
                const Real yc = 0.5 * (prob_lo[1] + prob_hi[1]);

                const Real c = 2.0*dx[0];
                const Real r  = std::sqrt((x-xc)*(x-xc) + (y-yc)*(y-yc));

                qsrc_arr(i, j, k) = parms_d.advection_moisture_rate*exp(-r / (c*c));
            });
        } else {
            // src is a function over Z
            ParallelFor(box, [=] AMREX_GPU_DEVICE (int i, int j, int k) {
                qsrc_arr(i, j, k) = 0.0;
            });
        }
    }
}
