#include "Utilities.H"

AMREX_GPU_DEVICE
void
pc_cmpTemp(
  const int i, const int j, const int k, amrex::Array4<amrex::Real> const& S)
{
  amrex::Real rhoInv = 1.0 / S(i, j, k, URHO);
  amrex::Real T = S(i, j, k, UTEMP);
  amrex::Real e = S(i, j, k, UEINT) * rhoInv;
  EOS::E2T(e, T);
  S(i, j, k, UTEMP) = T;
}

AMREX_GPU_DEVICE
void
pc_rst_int_e(
  const int i,
  const int j,
  const int k,
  amrex::Array4<amrex::Real> const& S,
  const int allow_small_energy,
  const int allow_negative_energy,
  const int dual_energy_update_E_from_e,
  const int verbose)
{
  if (allow_small_energy == 0) {
    const amrex::Real rhoInv = 1.0 / S(i, j, k, URHO);
    const amrex::Real Up = S(i, j, k, UMX) * rhoInv;
    const amrex::Real Vp = S(i, j, k, UMY) * rhoInv;
    const amrex::Real Wp = S(i, j, k, UMZ) * rhoInv;
    const amrex::Real ke = 0.5 * (Up * Up + Vp * Vp + Wp * Wp);
    const amrex::Real eden = S(i, j, k, UEDEN) * rhoInv;
    const amrex::Real eos_state_rho = S(i, j, k, URHO);
    const amrex::Real eos_state_T = SMALL_TEMP;
    amrex::Real eos_state_ei;
    EOS::T2Ei(eos_state_T, eos_state_ei);
    amrex::Real eos_state_e = eos_state_ei;
    const amrex::Real small_e = eos_state_e;
    if (eden < small_e) {
      if (S(i, j, k, UEINT) * rhoInv < small_e) {
        const amrex::Real eos_state_T =
          amrex::max(S(i, j, k, UTEMP), SMALL_TEMP);
        EOS::T2Ei(eos_state_T, eos_state_ei);
        eos_state_e = eos_state_ei;
        S(i, j, k, UEINT) = S(i, j, k, URHO) * eos_state_e;
      }
      S(i, j, k, UEDEN) = S(i, j, k, UEINT) + S(i, j, k, URHO) * ke;
    } else {
      const amrex::Real rho_eint = S(i, j, k, UEDEN) - S(i, j, k, URHO) * ke;
      if (
        (rho_eint > (S(i, j, k, URHO) * SMALL_E)) and
        ((rho_eint - S(i, j, k, URHO) * SMALL_E) /
         (S(i, j, k, UEDEN) - S(i, j, k, URHO) * SMALL_E)) > DUAL_ENERGY_ETA2) {
        S(i, j, k, UEINT) = rho_eint;
      }
      if (S(i, j, k, UEINT) * rhoInv < small_e) {
        const amrex::Real eos_state_T =
          amrex::max(S(i, j, k, UTEMP), SMALL_TEMP);
        EOS::T2Ei(eos_state_T, eos_state_ei);
        eos_state_e = eos_state_ei;
        if (dual_energy_update_E_from_e == 1) {
          S(i, j, k, UEDEN) =
            S(i, j, k, UEDEN) +
            (S(i, j, k, URHO) * eos_state_e - S(i, j, k, UEINT));
        }
        S(i, j, k, UEINT) = S(i, j, k, URHO) * eos_state_e;
      }
    }
  } else if (allow_negative_energy == 0) {
    const amrex::Real rhoInv = 1.0 / S(i, j, k, URHO);
    const amrex::Real Up = S(i, j, k, UMX) * rhoInv;
    const amrex::Real Vp = S(i, j, k, UMY) * rhoInv;
    const amrex::Real Wp = S(i, j, k, UMZ) * rhoInv;
    const amrex::Real ke = 0.5 * (Up * Up + Vp * Vp + Wp * Wp);
    if (S(i, j, k, UEDEN) < (S(i, j, k, URHO) * SMALL_E)) {
      if (S(i, j, k, UEINT) < (S(i, j, k, URHO) * SMALL_E)) {
        const amrex::Real eos_state_rho = S(i, j, k, URHO);
        const amrex::Real eos_state_T = SMALL_TEMP;
        amrex::Real eos_state_ei;
        EOS::T2Ei(eos_state_T, eos_state_ei);
        amrex::Real eos_state_e = eos_state_ei;
        S(i, j, k, UEINT) = S(i, j, k, URHO) * eos_state_e;
      }
      S(i, j, k, UEDEN) = S(i, j, k, UEINT) + S(i, j, k, URHO) * ke;
    } else {
      const amrex::Real rho_eint = S(i, j, k, UEDEN) - S(i, j, k, URHO) * ke;
      if (
        (rho_eint > (S(i, j, k, URHO) * SMALL_E)) and
        ((rho_eint - S(i, j, k, URHO) * SMALL_E) /
         (S(i, j, k, UEDEN) - S(i, j, k, URHO) * SMALL_E)) > DUAL_ENERGY_ETA2) {
        S(i, j, k, UEINT) = rho_eint;
      } else if (
        S(i, j, k, UEINT) > (S(i, j, k, URHO) * SMALL_E) and
        (dual_energy_update_E_from_e == 1)) {
        S(i, j, k, UEDEN) = S(i, j, k, UEINT) + S(i, j, k, URHO) * ke;
      } else if (S(i, j, k, UEINT) <= (S(i, j, k, URHO) * SMALL_E)) {
        const amrex::Real eos_state_rho = S(i, j, k, URHO);
        const amrex::Real eos_state_T = SMALL_TEMP;
        amrex::Real eos_state_ei;
        EOS::T2Ei(eos_state_T, eos_state_ei);
        amrex::Real eos_state_e = eos_state_ei;
        const amrex::Real eint_new = eos_state_e;
        /* Avoiding this for now due to GPU restricitions
        if (verbose) {
          amrex::Print() << std::endl;
          amrex::Print()
            << ">>> Warning: ERF_util.F90::reset_internal_energy "
            << " " << i << ", " << j << ", " << k << std::endl;
          amrex::Print() << ">>> ... resetting neg. e from EOS using SMALL_TEMP"
                         << std::endl;
          amrex::Print() << ">>> ... from ',S(i,j,k,UEINT)/S(i,j,k,URHO),' to "
                         << std::endl;
          amrex::Print() << eint_new << std::endl;
        }*/
        if (dual_energy_update_E_from_e == 1) {
          S(i, j, k, UEDEN) = S(i, j, k, UEDEN) +
                              (S(i, j, k, URHO) * eint_new - S(i, j, k, UEINT));
        }
        S(i, j, k, UEINT) = S(i, j, k, URHO) * eint_new;
      }
    }
  } else {
    const amrex::Real rho = S(i, j, k, URHO);
    const amrex::Real rhoInv = 1.0 / rho;
    const amrex::Real Up = S(i, j, k, UMX) * rhoInv;
    const amrex::Real Vp = S(i, j, k, UMY) * rhoInv;
    const amrex::Real Wp = S(i, j, k, UMZ) * rhoInv;
    const amrex::Real ke = 0.5 * (Up * Up + Vp * Vp + Wp * Wp);
    S(i, j, k, UEINT) = S(i, j, k, UEDEN) - rho * ke;
  }
}

