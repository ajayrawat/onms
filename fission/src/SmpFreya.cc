/* 
Copyright (c) 2006-2015 Lawrence Livermore National Security, LLC.
Produced at the Lawrence Livermore National Laboratory 
UCRL-CODE-224807.

All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

o   Redistributions of source code must retain the above copyright notice, this list of conditions and the disclaimer below.

o  Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the disclaimer (as noted below) in the documentation and/or other materials provided with the distribution.

o  Neither the name of the LLNS/LLNL nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LAWRENCE LIVERMORE NATIONAL SECURITY, LLC, THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
Additional BSD Notice

1. This notice is required to be provided under our contract with the U.S. Department of Energy (DOE). This work was produced at Lawrence Livermore National Laboratory under Contract No. DE-AC52-07NA27344 with the DOE. 

2. Neither the United States Government nor Lawrence Livermore National Security, LLC nor any of their employees, makes any warranty, express or implied, or assumes any liability or responsibility for the accuracy, completeness, or usefulness of any information, apparatus, product, or process disclosed, or represents that its use would not infringe privately-owned rights. 

3. Also, reference herein to any specific commercial products, process, or services by trade name, trademark, manufacturer or otherwise does not necessarily constitute or imply its endorsement, recommendation, or favoring by the United States Government or Lawrence Livermore National Security, LLC. The views and opinions of authors expressed herein do not necessarily state or reflect those of the United States Government or Lawrence Livermore National Security, LLC, and shall not be used for advertising or product endorsement purposes.
*/


/*
 This interfaces the LLNL fission library to FREYA
 Jerome Verbeke, LLNL 9-9-2013
 */
#define maxN 30       /* The maximum number of ejectiles generated by FREYA */

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "fissionEvent.h"

extern "C" {
   extern int msfreya_setup_();
   extern int msfreya_event_(int,float,float,float*,int*,int*,float*,int*,int*,float*,int*,float*,int*);
   extern int msfreya_getniso_(int *,int *);
   extern int msfreya_getzas_(int *,int *);
   extern float msfreya_sepn_cvt_(int, int, int);
   extern float msfreya_gsmassn_(int, int);
   extern int msfreya_getlasterror_(char *, int *);
   extern int msfreya_geterrors_(char *, int *);
   extern int msfreya_reseterrorflag_();
   extern bool msfreya_errorflagset_();
   extern int msfreya_usehostrng_();
}

void fissionEvent::saveFREYANeutron(float* pp) {
   static float MeVperctoMeV=5.32934e-4;

   neutronDircosu[neutronNu] = pp[0];
   neutronDircosv[neutronNu] = pp[1];
   neutronDircosw[neutronNu] = pp[2];
   // neutronEnergies[neutronNu] = pp[3];
   double norm = 
           neutronDircosu[neutronNu]*neutronDircosu[neutronNu]
          +neutronDircosv[neutronNu]*neutronDircosv[neutronNu]
          +neutronDircosw[neutronNu]*neutronDircosw[neutronNu];
   neutronEnergies[neutronNu] = norm*MeVperctoMeV;
   norm = 1./sqrt(norm);
   neutronDircosu[neutronNu] *= norm;
   neutronDircosv[neutronNu] *= norm;
   neutronDircosw[neutronNu] *= norm;
   neutronVelocities[neutronNu] = SmpNVel(neutronEnergies[neutronNu]);
   neutronNu++;
}

void fissionEvent::saveFREYAPhoton(float* pp) {
   photonDircosu[photonNu] = pp[0];
   photonDircosv[photonNu] = pp[1];
   photonDircosw[photonNu] = pp[2];
   photonEnergies[photonNu] = pp[3];
   double norm = 
           photonDircosu[photonNu]*photonDircosu[photonNu]
          +photonDircosv[photonNu]*photonDircosv[photonNu]
          +photonDircosw[photonNu]*photonDircosw[photonNu];
   norm = 1./sqrt(norm);
   photonDircosu[photonNu] *= norm;
   photonDircosv[photonNu] *= norm;
   photonDircosw[photonNu] *= norm;
   photonVelocities[photonNu] = SmpPVel(); 
   photonNu++;
//   std::cout << photonNu << " photon of energy " << pp[4] << std::endl;
}

bool fissionEvent::SmpFreya(double ePart, int iso, int fissiontype) {
/*
  Description
    Determine the numbers and energies of neutrons and photons emitted by 
    induced fission. FREYA is doing all the work. Current version works 
    for spontaneous fission and induced fission for incoming neutron/photon 
    energies less than 20~30 MeV (depending on the isotopes), for a very 
    limited number of isotopes.
  Input
    ePart       - energy of incoming particle
    iso         - isotope
    fissiontype - 0: spontaneous fission
                  1: neutron-induced fission
                  2: photon-induced fission
  Output
    neutronEnergies
              - energies of the emitted fission neutrons
    photonEnergies
              - energies of the emitted fission photons
    neutronVelocities
              - velocities of the emitted fission neutrons
    photonVelocities
              - velocities of the emitted fission photons
    neutronDircosu, neutronDircosv, neutronDircosw
              - direction cosines of the emitted fission neutrons
    photonDircosu, photonDircosv, photonDircosw
              - direction cosines of the emitted fission photons
*/
   static bool firstcall = true;
   static bool setupfailed = false;

   static int niso = 0;   // Number of fission isotopes
   static int nisosf = 0; // Number of spontaneous fission isotopes
   static int nisoif = 0; // Number of induced fission isotopes

   static int* ZAs;       // ZA's of fission isotopes
   static int* fistypes;  // types of fission [spontaneous (0), induced (1)

   static int lastiKm1=0; // index of last fission/isotope used m1 stands for 
                          // "minus 1". If iK=i in FORTRAN, iK=i-1 in c++

   int iKm1=0;            // index of current fission/isotope


   if (fissiontype==2) return false; // FREYA does not handle photofission yet

   if (setupfailed) return false;

   // initialization phase
   if (firstcall) {
      // call Freya setup subroutine
      msfreya_reseterrorflag_();
      msfreya_setup_();
      if (!handle_freya_error()) {
        setupfailed = true;
        return false;
      }
      msfreya_getniso_(&nisosf,&nisoif);
      niso=nisosf+nisoif;
      // allocate memory to store ZA's for spontaneous and neutron-induced
      // fissions
      ZAs = new int [niso];
      fistypes = new int [niso];
      // Populate ZAs and fistypes
      msfreya_getzas_(&(ZAs[0]),&(fistypes[0]));
      // use random number generator rngdptr()
      msfreya_usehostrng_();
      firstcall = false;
   } // if (firstcall)


   // if the compound nucleus is ZA, the original nucleus was
   //   ZA for photofission
   //   Z(A-1) for neutron-induced fission
   // treat photofission as if it were neutron-induced fission
   if (fissiontype==2) iso--;

   // Find the index of the fission/isotope
   // ...check if this is the last fission/isotope used
   if ((iso == ZAs[lastiKm1]) && ((fissiontype==0) == (fistypes[lastiKm1]==0))) {
     iKm1=lastiKm1;
   } else {
     // ...it is not, find the new iKm1 for iso ZA
     bool foundfission=false;
     for (iKm1=0; iKm1<niso; iKm1++) 
       if (iso == ZAs[iKm1] && ((fissiontype==0) == (fistypes[iKm1]==0))) {
         foundfission=true;
         break;
       }
     if (!foundfission) return false; // FREYA cannot handle this fission 
                                      // type for this isotope
     lastiKm1=iKm1;
   }
   // ... found index of fission/isotope iKm1
   int iK=iKm1+1; // FORTRAN indexing
   int Z=iso/1000;
   int freyaA=iso-1000*Z;
   // watch out! in freya, the A for induced fission is the A of the 
   // compound nucleus (for induced fission, add 1 neutron to the nucleus)
   freyaA+=(fissiontype==0)?0:1; 

   // Compute nucleus excitation energy for this event
   float eps0;
   float En;
   switch (fissiontype) {
      case 0:
         // spontaneous fission
         eps0 = 0.;
         En=0.;
         break;
      case 1:
         // neutron-induced fission
      case 2:
         // photon-induced fission
         msfreya_reseterrorflag_();
         float sepni;
         sepni = msfreya_sepn_cvt_(iK,Z,freyaA);
         // Jerome, check with J�rgen for photofission
         if (!handle_freya_error()) return false;

         if (fissiontype==1) {
            // neutron-induced fission
            eps0 = sepni+ePart;
            En=ePart;
         } else if (fissiontype==2) {
            // photon-induced fission
            eps0 = ePart;
            En=ePart-sepni;
            if (En<0) En=0.;
         }
         break;
      default:
         // fission type not supported
         return false;
         break;
   }
   // ...generate fission event
   // declare those, msfreya_event needs them
   float V0[3]; // velocity of the initial nucleus
   for (int i=0; i<3; i++) V0[i]=0; // nucleus at rest

   float P0[5]; // excited energy, momentum and kinetic energy
                // of nucleus before interaction
   float P1[5]; // excited energy, momentum and kinetic energy
                // of fission product 1
   float P2[5]; // excited energy, momentum and kinetic energy
                // of fission product 2
   int Z1, A1;  // Charge & mass number of product 1
   int Z2, A2;  // Charge & mass number of product 2

   msfreya_reseterrorflag_();
   float W0=msfreya_gsmassn_(Z,freyaA);  // ground-state mass of nucleus
   if (!handle_freya_error()) return false;

   P0[0]=W0+eps0;               // Rest energy of init nucleus
   // float g0=1.0/sqrt(1.0-V0[1]*V0[1]-V0[2]*V0[2]-V0[3]*V0[3]); // gamma0
   float g0=1.0;                // gamma0
   P0[4]=g0*P0[0];              // Total energy of init nucleus
   // float T0=0.;
   for (int i=0; i<3; i++) {
     P0[i]=P0[4]*V0[i];         // Momentum of initial nucleus
     // T0 += V0[i]*V0[i];
   }
   // T0=0.5*P0[0]*T0;          // NR kin erg of initial nucleus
   // float E0=P0(4)-P0(0);     // Kinetic energy of initial nucleus

   int mult;                    // Number of particles emitted
   float particles [4*3*maxN];  // their momentum and kinetic energy
   // float particles [5*3*maxN];
   int ptypes [3*maxN];         // their type: 0(g) & 1(n)
   
   msfreya_reseterrorflag_();
   msfreya_event_(iK,En,eps0,&(P0[0]),&Z1,&A1,&(P1[0]),&Z2,&A2,&(P2[0]),&mult,&(particles[0]),&(ptypes[0]));
   if (!handle_freya_error()) return false;

   neutronNu=0;
   photonNu=0;
   for (int i=0; i<mult; i++){
      if (ptypes[i] == 0) photonNu++; // photon
      if (ptypes[i] == 1) neutronNu++; // neutron
   }

// We allocate the necessary memory...
   allocateMem(neutronNu, photonNu);
// ...then we fill in the arrays
   neutronNu=0;
   photonNu=0;
   for (int i=0; i<mult; i++){
      if (ptypes[i] == 0) saveFREYAPhoton(&(particles[4*i])); // photon
      if (ptypes[i] == 1) saveFREYANeutron(&(particles[4*i])); // neutron
   }
   return true;
}

bool fissionEvent::handle_freya_error() {
   if (msfreya_errorflagset_()) return false;
   return true;
}

void fissionEvent::getFREYAerrors(int* length, char* error) {
   if (correlationoption == 3) msfreya_geterrors_(error,length);
   else *length=1;
}
