/// \file  TrackMomentumCalculator.h
//  \author  sowjanyag@phys.ksu.edu

#ifndef TrackMomentumCalculator_H
#define TrackMomentumCalculator_H

#include "iostream"
#include "vector"
#include "TMath.h"
#include "lardataobj/RecoBase/Track.h"
#include "TGraph.h"
#include "TGraphErrors.h"
#include "TAxis.h"
#include "TPolyLine3D.h"
#include "TSpline.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "fhiclcpp/ParameterSet.h" 
#include "canvas/Persistency/Common/Ptr.h" 
#include "canvas/Persistency/Common/PtrVector.h" 
#include "art/Framework/Services/Registry/ServiceHandle.h" 
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "Minuit2/MnUserParameterState.h"
#include "Minuit2/Minuit2Minimizer.h"
#include "Minuit2/FunctionMinimum.h" 
#include "Minuit2/MnMigrad.h" 
#include "Minuit2/MnUserParameters.h" 
#include "Minuit2/MnPrint.h" 
#include "Minuit2/FCNBase.h" 
#include "Math/Minimizer.h"
#include "Math/Factory.h"
#include "larcore/CoreUtils/quiet_Math_Functor.h"
#include "TMatrixDSym.h"
#include "TMatrixDSymEigen.h"
#include "TVector3.h"
#include <math.h>
#include <cmath>

using namespace std;

// Global variables/input 

// A. ---> for the TMinuit2 chi^2 minimization !

Double_t xmeas[30]; Double_t ymeas[30]; Double_t eymeas[30]; Int_t nmeas;

// B. ---> For the LLHD raster scan !

// ..

namespace trkf{
  
  class TrackMomentumCalculator
  {
    Int_t n;
  
    Double_t x[50000]; Double_t y[50000]; Double_t z[50000];
        
    Int_t n_reco;
  
    Float_t x_reco[50000]; Float_t y_reco[50000]; Float_t z_reco[50000];
        
    Float_t seg_size; Float_t seg_stop; Int_t n_seg;
    
    Float_t x_seg[50000]; Float_t y_seg[50000]; Float_t z_seg[50000];
            
    TVector3 basex; TVector3 basey; TVector3 basez; 
       
    std::vector<Float_t> segx; std::vector<Float_t> segy; std::vector<Float_t> segz; 
  
    std::vector<Float_t> segnx; std::vector<Float_t> segny; std::vector<Float_t> segnz;
    
    std::vector<Float_t> segL;
    
    std::vector<Float_t> azx0; std::vector<Float_t> azy0;
        
    Double_t find_angle( Double_t vz, Double_t vy );
    
    Float_t steps_size; Int_t n_steps; std::vector<Float_t> steps;
    
    Float_t steps_size2;
        
    Float_t kcal;
    
    std::vector<Float_t> dthij; std::vector<Float_t> dEi; std::vector<Float_t> dEj; std::vector<Float_t> ind;

    Double_t minLength;

    Double_t maxLength;
        
  public:
    
    // Constructor and destructor  // 
    
    TrackMomentumCalculator();
    
    virtual ~TrackMomentumCalculator() {}
    
    double GetTrackMomentum(double trkrange, int pdg);
    
    TPolyLine3D *gr_xyz; TGraph *gr_xy; TGraph *gr_yz; TGraph *gr_xz; 
    
    Int_t GetTracks( const std::vector<Float_t> &xxx, const std::vector<Float_t> &yyy, const std::vector<Float_t> &zzz );
        
    TPolyLine3D *gr_reco_xyz; TGraph *gr_reco_xy; TGraph *gr_reco_yz; TGraph *gr_reco_xz; 
    
    Int_t GetRecoTracks( const std::vector<Float_t> &xxx, const std::vector<Float_t> &yyy, const std::vector<Float_t> &zzz );
        
    TPolyLine3D *gr_seg_xyz; TGraph *gr_seg_xy; TGraph *gr_seg_yz; TGraph *gr_seg_xz; 
    
    Int_t GetSegTracks( const std::vector<Float_t> &xxx, const std::vector<Float_t> &yyy, const std::vector<Float_t> &zzz );
    
    Int_t GetSegTracks2( const std::vector<Float_t> &xxx, const std::vector<Float_t> &yyy, const std::vector<Float_t> &zzz );
    
    void GetDeltaThetaRMS( Double_t &mean, Double_t &rms, Double_t &rmse, Double_t thick );
    
    TGraphErrors *gr_meas;

    TGraph *KEvsR;
    
    TSpline3 *KEvsR_spline3;

    Double_t GetMomentumMultiScatterChi2( const art::Ptr<recob::Track> &trk );
    
    Double_t p_mcs; Double_t p_mcs_e; Double_t chi2;
    
    Int_t GetDeltaThetaij( std::vector<Float_t> &ei, std::vector<Float_t> &ej, std::vector<Float_t> &th, Double_t thick, std::vector<Float_t> &ind );
    
    Double_t my_g( Double_t xx, Double_t Q, Double_t s );
        
    Double_t my_mcs_llhd( Double_t x0, Double_t x1 );
        
    Double_t GetMomentumMultiScatterLLHD( const art::Ptr<recob::Track> &trk );
    
    Double_t p_mcs_2; Double_t LLbf;
    
    // Double_t GetMuMultiScatterLLHD( const art::Ptr<recob::Track> &trk );
    
    // Double_t GetMuMultiScatterLLHD2( const recob::Track &trk );
    
    Double_t GetMuMultiScatterLLHD3( const art::Ptr<recob::Track> &trk, bool dir );
    
    TVector3 GetMultiScatterStartingPoint( const art::Ptr<recob::Track> &trk );

    void SetMinLength(double minLen) {minLength = minLen;}

    void SetMaxLength(double maxLen) {maxLength = maxLen;}

  };
  
  
} //namespace trkf

#endif // TrackMomentumCalculator_H  
