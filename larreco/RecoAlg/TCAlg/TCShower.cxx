#include "larreco/RecoAlg/TCAlg/TCShower.h"

struct SortEntry{
  unsigned int index;
  float length;
};

bool greaterThan (SortEntry c1, SortEntry c2) { return (c1.length > c2.length);}
bool lessThan (SortEntry c1, SortEntry c2) { return (c1.length < c2.length);}


namespace tca {

  ////////////////////////////////////////////////
  void Find3DShowerEndPoints(TjStuff& tjs, const geo::TPCID& tpcid)
  {
    
    if(tjs.ShowerTag[0] < 0) return;
    
    bool prt = (tjs.ShowerTag[12] == 3);
    
    if(prt) mf::LogVerbatim("TC")<<"Inside Find3DShowerEndPoints";
    
    unsigned int cstat = tpcid.Cryostat;
    unsigned int tpc = tpcid.TPC;

    for(auto& im : tjs.matchVecPFPList) {
      // a reference to a set of 3D matched trajectories
      auto& ms = tjs.matchVec[im];
      if(ms.Count == 0) continue;
      // Only consider shower Tjs
      if(ms.PDGCode != 1111) continue;
      // ensure we are in the correct tpcid using the first Tj CTP
      unsigned short it1 = ms.TjIDs[0] - 1;
      geo::PlaneID iPlnID = DecodeCTP(tjs.allTraj[it1].CTP);
      if(iPlnID.Cryostat != cstat) continue;
      if(iPlnID.TPC != tpc) continue;
      // find the best order so we get the most likely pair on the first try.
      // This requires knowing the X positions
      std::vector<float> xpo(ms.TjIDs.size());
      for(unsigned short ii = 0; ii < ms.TjIDs.size(); ++ii) {
        // find this ID in the shower struct vector
        unsigned short issIndex = GetCotsIndex(tjs, ms.TjIDs[ii]);
        if(issIndex == USHRT_MAX) continue;
        ShowerStruct& iss = tjs.cots[issIndex];
        TrajPoint& itp = tjs.allTraj[iss.ShowerTjID - 1].Pts[0];
        geo::PlaneID iPlnID = DecodeCTP(itp.CTP);
        xpo[ii] = tjs.detprop->ConvertTicksToX(itp.Pos[1] / tjs.UnitsPerTick, iPlnID);
      } // ii
      std::vector<float> matchWght(ms.TjIDs.size(), 100);
      for(unsigned short ii = 0; ii < ms.TjIDs.size() - 1; ++ii) {
        unsigned short issIndex = GetCotsIndex(tjs, ms.TjIDs[ii]);
        if(issIndex == USHRT_MAX) continue;
        ShowerStruct& iss = tjs.cots[issIndex];
        for(unsigned short jj = ii + 1; jj < ms.TjIDs.size(); ++jj) {
          unsigned short jssIndex = GetCotsIndex(tjs, ms.TjIDs[jj]);
          if(jssIndex == USHRT_MAX) continue;
          ShowerStruct& jss = tjs.cots[jssIndex];
          float dxpull = (xpo[ii] - xpo[jj]) / 0.3;
          // weight by energy match
          float eerr = iss.Energy;
          if(jss.Energy > eerr) eerr = jss.Energy;
          // Assume a 30% error on the energy match
          eerr *= 0.3;
          float epull = (iss.Energy - jss.Energy) / eerr;
          float wght = 0.5 * sqrt(dxpull * dxpull + epull * epull);
          if(wght < matchWght[ii]) matchWght[ii] = wght;
          if(wght < matchWght[jj]) matchWght[jj] = wght;
        } // jj
      } // ii
      std::vector<SortEntry> sortVec(matchWght.size());
      for(unsigned short ii = 0; ii < matchWght.size(); ++ii) {
        sortVec[ii].index = ii;
        sortVec[ii].length = matchWght[ii];
      }
      std::sort(sortVec.begin(), sortVec.end(), lessThan);
      // re-order TjIDs
      std::vector<int> tmp(ms.TjIDs.size());
      for(unsigned short ii = 0; ii < matchWght.size(); ++ii) tmp[ii] = ms.TjIDs[sortVec[ii].index];
      ms.TjIDs = tmp;

      // sum of the matching weights
      float wsum = 0;
      ms.sXYZ = {0, 0, 0};
      ms.sDir = {0, 0, 0};
      // the number of end position entries
      unsigned short ne = 0;
      ms.eXYZ = {0, 0, 0};
      TVector3 prevDir = {0, 0, 0};
      // count the number of times each Tj is matched
      std::vector<unsigned short> cntUsed(ms.TjIDs.size());
      
      for(unsigned short ii = 0; ii < ms.TjIDs.size() - 1; ++ii) {
        // find this ID in the shower struct vector
        unsigned short issIndex = GetCotsIndex(tjs, ms.TjIDs[ii]);
        if(issIndex == USHRT_MAX) continue;
        ShowerStruct& iss = tjs.cots[issIndex];
        Trajectory& itj = tjs.allTraj[iss.ShowerTjID - 1];
        TrajPoint& itp = itj.Pts[0];
        geo::PlaneID iPlnID = DecodeCTP(itp.CTP);
        unsigned short iPln = iPlnID.Plane;
        for(unsigned short jj = ii + 1; jj < ms.TjIDs.size(); ++jj) {
          unsigned short jssIndex = GetCotsIndex(tjs, ms.TjIDs[jj]);
          if(jssIndex == USHRT_MAX) continue;
          ShowerStruct& jss = tjs.cots[jssIndex];
          Trajectory& jtj = tjs.allTraj[jss.ShowerTjID - 1];
          TrajPoint& jtp = jtj.Pts[0];
          // check traj point X direction compatibility
          if(std::abs(itp.Dir[1]) > 0.1 && std::abs(jtp.Dir[1]) > 0.1 && itp.Dir[1] * jtp.Dir[1] < 0) {
            if(prt) mf::LogVerbatim("TC")<<" FSEP: Incompatible Tp X directions "<<itp.Dir[1]<<" "<<jtp.Dir[1]<<" TjIDs "<<itj.ID<<" "<<jtj.ID;
            continue;
          }
          geo::PlaneID jPlnID = DecodeCTP(jtp.CTP);
          unsigned short jPln = jPlnID.Plane;
          TVector3 pos, dir;
          TrajPoint3D(tjs, itp, jtp, pos, dir);
          if(dir.X() > 1) {
            if(prt) mf::LogVerbatim("TC")<<" FSEP: TrajPoint3D failed using points "<<iPln<<":"<<PrintPos(tjs, itp.Pos)<<" "<<jPln<<":"<<PrintPos(tjs, jtp.Pos);
            continue;
          }
          float wght = 2 / (matchWght[ii] + matchWght[jj]);
          // de-weight matches in which the start charge isn't known
          if(iss.StartChg == 0 || jss.StartChg == 0) wght /= 0.1;
          wsum += wght;
          ms.sXYZ[0] += wght * pos[0];
          ms.sXYZ[1] += wght * pos[1];
          ms.sXYZ[2] += wght * pos[2];
          // don't allow sign changes to the major direction cosine.
          if(prevDir[0] == 0) {
            prevDir = dir;
          } else {
            // a valid direction cosine exists
            for(unsigned short ixyz = 0; ixyz < 3; ++ixyz) {
              if(std::abs(prevDir[ixyz]) > 0.5) {
                if(prevDir[ixyz] * dir[ixyz] < 0) dir *= -1;
                break;
              }
            }
          } //  check for direction sign change
          ms.sDir += wght * dir;
          // now find eXYZ. This is done only to get the 3D shower direction correct
          TrajPoint& ietp = itj.Pts[2];
          TrajPoint& jetp = jtj.Pts[2];
          float ieX = tjs.detprop->ConvertTicksToX(ietp.Pos[1]/tjs.UnitsPerTick, iPlnID);
          float jeX = tjs.detprop->ConvertTicksToX(jetp.Pos[1]/tjs.UnitsPerTick, jPlnID);
          double yp, zp;
          tjs.geom->IntersectionPoint(std::nearbyint(ietp.Pos[0]), std::nearbyint(jetp.Pos[0]), iPln, jPln, cstat, tpc, yp, zp);
          ++cntUsed[ii];
          ++cntUsed[jj];
          if(prt) {
            mf::LogVerbatim myprt("TC");
            myprt<<" FSEP: itp "<<iPln<<":"<<PrintPos(tjs, itp.Pos)<<std::fixed<<std::setprecision(2)<<" Angle "<<itp.Ang;
            myprt<<" jtp "<<jPln<<":"<<PrintPos(tjs, jtp.Pos)<<" Angle "<<jtp.Ang;
            myprt<<" start pos "<<std::fixed<<std::setprecision(1)<<pos[0]<<" "<<pos[1]<<" "<<pos[2];
            myprt<<" dir "<<std::fixed<<std::setprecision(2)<<dir[0]<<" "<<dir[1]<<" "<<dir[2];
            myprt<<" wght "<<wght;
            myprt<<" end pos "<<std::fixed<<std::setprecision(1)<<0.5 * (ieX + jeX)<<" "<<yp<<" "<<zp;
          }
          ++ne;
          ms.eXYZ[0] += 0.5 * (ieX + jeX);
          ms.eXYZ[1] += yp;
          ms.eXYZ[2] += zp;
        } // jj
      } // ii
      if(wsum == 0 || ne == 0) {
        if(prt) mf::LogVerbatim("TC")<<" FSEP: Failed to find shower start or end. "<<wsum<<" "<<ne<<" Skip this match";
        continue;
      }
      for(auto& xyz : ms.sXYZ) xyz /= wsum;
      for(auto& xyz : ms.eXYZ) xyz /= (float)ne;
      if(ms.sDir.Mag() != 0) ms.sDir.SetMag(1);
      // correct the direction using a large direction cosine
      for(unsigned short ixyz = 0; ixyz < 3; ++ixyz) {
        if(std::abs(ms.sDir[ixyz]) > 0.5) {
          if(ms.eXYZ[ixyz] > ms.sXYZ[ixyz] && ms.sDir[ixyz] < 0) ms.sDir *= -1;
          break;
        }
      } // ixyz
      // We can do better on the end point by using the shower direction instead of using the shower Tj end points.
      // This ensures that the shower start and end lie on the shower axis
      float length = 0;
      for(unsigned short ixyz = 0; ixyz < 3; ++ixyz) {
        length += (ms.eXYZ[ixyz] - ms.sXYZ[ixyz]) * (ms.eXYZ[ixyz] - ms.sXYZ[ixyz]);
      } // ixyz
      length = sqrt(length);
      for(unsigned short ixyz = 0; ixyz < 3; ++ixyz) ms.eXYZ[ixyz] = ms.sXYZ[ixyz] + ms.sDir[ixyz] * length;
      // Check for a shower that wasn't used in the match
      unsigned short notUsedShower = USHRT_MAX;
      for(unsigned short ii = 0; ii < cntUsed.size(); ++ii) if(cntUsed[ii] == 0) notUsedShower = ii;
      if(notUsedShower < ms.TjIDs.size()) {
        unsigned short cotsIndex = GetCotsIndex(tjs, ms.TjIDs[notUsedShower]);
        // Zero the charge because it isn't reliable
        if(cotsIndex < tjs.cots.size()) tjs.cots[notUsedShower].StartChg = 0;
      }
      if(prt) {
        mf::LogVerbatim myprt("TC");
        myprt<<" FSEP: TjIDs";
        for(auto& tjid : ms.TjIDs) myprt<<" "<<tjid;
        myprt<<" Start "<<std::fixed<<std::setprecision(1)<<ms.sXYZ[0]<<" "<<ms.sXYZ[1]<<" "<<ms.sXYZ[2];
        myprt<<" End "<<std::fixed<<std::setprecision(1)<<ms.eXYZ[0]<<" "<<ms.eXYZ[1]<<" "<<ms.eXYZ[2];
        myprt<<" 3D Dir "<<std::setprecision(2)<<ms.sDir.X()<<" "<<ms.sDir.Y()<<" "<<ms.sDir.Z();
        if(notUsedShower == USHRT_MAX) {
          myprt<<" All showers matched";
        } else {
          myprt<<" Note: Shower "<<notUsedShower<<" was not used in the 3D match. It is probably bad.";
        }
      } // prt
//      std::cout<<"FSEP: Start "<<std::fixed<<std::setprecision(1)<<ms.sXYZ[0]<<" "<<ms.sXYZ[1]<<" "<<ms.sXYZ[2];
//      std::cout<<" Dir "<<std::setprecision(2)<<ms.sDir.X()<<" "<<ms.sDir.Y()<<" "<<ms.sDir.Z()<<"\n";
    } // im

  } // Find3DShowerEndPoints

  ////////////////////////////////////////////////
  void MakeShowers(TjStuff& tjs)
  {
    // Fill 3D shower variables. First look for matching shower Tjs, then use this
    // information to find matching parent Tjs
    
    if(tjs.ShowerTag[0] < 0) return;
    
    // Get the calibration constants
    
    bool prt = (tjs.ShowerTag[12] >= 0);
    
    int shID = 0;
    std::vector<unsigned int> tHits;
    for(unsigned short ipfp = 0; ipfp < tjs.matchVecPFPList.size(); ++ipfp) {
      unsigned short imv = tjs.matchVecPFPList[ipfp];
      auto& ms = tjs.matchVec[imv];
      if(ms.TjIDs.empty()) continue;
      if(ms.PDGCode == 13) continue;
      if(prt) {
        mf::LogVerbatim myprt("TC");
        myprt<<"MS: "<<ipfp<<" ParentMSIndex "<<ms.ParentMSIndex<<" PDGCode "<<ms.PDGCode<<" Dtr size "<<ms.DtrIndices.size();
        myprt<<" TjIDs:";
        for(auto& tjID : ms.TjIDs) myprt<<" "<<tjID;
      } // prt
      // look for matched shower Tjs
      if(ms.PDGCode != 1111) continue;
      if(prt) {
        mf::LogVerbatim myprt("TC");
        myprt<<" Shower Tj";
        for(auto& tjID : ms.TjIDs) {
          Trajectory& tj = tjs.allTraj[tjID-1];
          unsigned short endPt = tj.EndPt[0];
          myprt<<"\n "<<tj.ID<<" start "<<PrintPos(tjs, tj.Pts[endPt]);
          endPt = tj.EndPt[1];
          myprt<<" "<<tj.ID<<" end "<<PrintPos(tjs, tj.Pts[endPt]);
          auto cotIndex = GetCotsIndex(tjs, tjID);
          myprt<<" cotIndex "<<cotIndex;
          if(cotIndex == USHRT_MAX) {
            myprt<<" Not a shower Tj. Ignore it";
          } else {
            ShowerStruct& ss = tjs.cots[cotIndex];
            myprt<<" StartChg "<<(int)ss.StartChg;
          }
        } //  tjID
      } // prt
      ++shID;
      ShowerStruct3D ss3;
      ss3.Energy.resize(tjs.NumPlanes);
      ss3.EnergyErr.resize(tjs.NumPlanes);
      ss3.MIPEnergy.resize(tjs.NumPlanes);
      ss3.MIPEnergyErr.resize(tjs.NumPlanes);
      ss3.dEdx.resize(tjs.NumPlanes);
      ss3.dEdxErr.resize(tjs.NumPlanes);
      ss3.ID = shID;
      // fill the start position
      for(unsigned short ixyz = 0; ixyz < 3; ++ixyz) ss3.Pos[ixyz] = ms.sXYZ[ixyz];
      // and direction
      ss3.Dir = ms.sDir;
      if(prt) mf::LogVerbatim("TC")<<" Shower start "<<ss3.Pos.X()<<" "<<ss3.Pos.Y()<<" "<<ss3.Pos.Z()<<" dir "<<ss3.Dir.X()<<" "<<ss3.Dir.Y()<<" "<<ss3.Dir.Z();
      ss3.DirErr = ms.sDirErr;
      // Find the shower length.
      ss3.Len = 0;
      for(unsigned short ixyz = 0; ixyz < 3; ++ixyz) {
        double dpos = ms.eXYZ[ixyz] - ms.sXYZ[ixyz];
        ss3.Len += dpos * dpos;
      }
      ss3.Len = sqrt(ss3.Len);
      // We need the shower structs to fill the variables in each plane
      for(unsigned short ii = 0; ii < ms.TjIDs.size(); ++ii) {
        unsigned short istjID = ms.TjIDs[ii];
        Trajectory& stj = tjs.allTraj[istjID - 1];
        tHits = PutTrajHitsInVector(stj, kUsedHits);
        ss3.Hits.insert(ss3.Hits.end(), tHits.begin(), tHits.end());
        // find this ID in the shower struct vector
        unsigned short cotIndex = GetCotsIndex(tjs, istjID);
        if(cotIndex == USHRT_MAX) continue;
        geo::PlaneID planeID = DecodeCTP(tjs.cots[cotIndex].CTP);
        unsigned short iPln = planeID.Plane;
        // TODO Calculate energy using caloAlg
        ss3.Energy[iPln] = tjs.cots[cotIndex].Energy;
        // This is just a guess for now
        ss3.EnergyErr[iPln] = 0.3 * tjs.cots[cotIndex].Energy;
        // This is probably wrong also...
        ss3.MIPEnergy[iPln] = ss3.Energy[iPln] / 2.3;
        ss3.MIPEnergyErr[iPln] = ss3.EnergyErr[iPln] / 2.3;
        ss3.dEdx[iPln] = 0;
        // Calculate dE/dx
        if(tjs.cots[cotIndex].StartChg > 0) {
          double angleToVert = tjs.geom->WireAngleToVertical(tjs.geom->View(planeID), planeID.TPC, planeID.Cryostat) - 0.5 * ::util::pi<>();
          double cosgamma = std::abs(std::sin(angleToVert) * ss3.Dir.Y() + std::cos(angleToVert) * ss3.Dir.Z());
          if(cosgamma == 0) continue;
          double dx = tjs.geom->WirePitch(planeID) / cosgamma;
          // dQ was normalized to 1 WSE (1 wire spacing equivalent) in FindStartChg 
          double dQ = tjs.cots[cotIndex].StartChg;
          std::cout<<iPln<<std::fixed<<" dQ "<<(int)dQ<<" dx "<<dx;
          // Get the time using the shower charge center position
          double time = stj.Pts[1].Pos[1] / tjs.UnitsPerTick;
          double T0 = 0;
          dQ *= tjs.caloAlg->LifetimeCorrection(time, T0);
          std::cout<<" corrected "<<(int)dQ;
          std::cout<<" dQ/dx "<<(int)dQ/dx;
          // Convert to number of electrons and make recombination correction for a 1 MIP particle.
          // These are hard-coded numbers that are highly unlikely to change by a significant amount.
          // This is a good approximation for electromagnetic showers but wouldn't be for hadronic showers.
          double nElectrons = tjs.caloAlg->ElectronsFromADCArea(dQ, iPln) / 0.63;
          std::cout<<" nElectrons "<<std::fixed<<(int)nElectrons;
          double dedx = nElectrons * 23.6E-6 / dx;
          ss3.dEdx[iPln] = dedx;
          // this is a bit of a fake
          ss3.dEdxErr[iPln] = dedx * tjs.cots[cotIndex].StartChgErr / tjs.cots[cotIndex].StartChg;
          std::cout<<" dedx "<<dedx<<" +/- "<<ss3.dEdxErr[iPln]<<"\n";
          if(prt) mf::LogVerbatim("TC")<<"MS: cotIndex "<<cotIndex<<" plane "<<iPln<<" plane "<<iPln<<" dQ "<<(int)dQ<<" dx "<<dx<<" dE/dx "<<ss3.dEdx[iPln]<<" +/- "<<ss3.dEdxErr[iPln];
        }
      } // ii
      // TODO Calculate the opening angle here 
      ss3.OpenAngle = 0.1;
      // We shouldn't define the ss3 Hit vector until hit merging is done
      tjs.showers.push_back(ss3);
      
    } // ipfp
  } // MakeShowers
  
  ////////////////////////////////////////////////
  bool FindShowers3D(TjStuff& tjs, const geo::TPCID& tpcid)
  {
    // Find 2D showers using 3D-matched trajectories. This returns true if showers were found
    // which requires re-doing the 3D trajectory match
    
    if(tjs.ShowerTag[0] != 2) return false;
    
    bool prt = true;
    unsigned short dbgPlane = tjs.ShowerTag[12];
    CTP_t dbgCTP = EncodeCTP(tpcid.Cryostat, tpcid.TPC, dbgPlane);

    if(tjs.matchVec.empty()) {
      if(prt) mf::LogVerbatim("TC")<<"FindShowers3D: Give up because matchVec is empty";
      return false;
    }
    
    geo::TPCGeo const& TPC = tjs.geom->TPC(tpcid);
    // check for already-existing showers
    for(unsigned short plane = 0; plane < TPC.Nplanes(); ++plane) {
      CTP_t inCTP = EncodeCTP(tpcid.Cryostat, tpcid.TPC, plane);
      for(auto& ss : tjs.cots) if(ss.CTP == inCTP) return false;
    }
    
    // rebuild the hit range references if necessary
    if(tpcid != tjs.TPCID && !FillWireHitRange(tjs, tpcid, false)) {
      std::cout<<"FindShowers3D: Fatal error from FillWireHitRange\n";
      return false;
    }
    
    // lists of Tj IDs in plane, (list1, list2, list3, ...)
    std::vector<std::vector<std::vector<int>>> bigList(tjs.NumPlanes);
    for(unsigned short plane = 0; plane < TPC.Nplanes(); ++plane) {
      CTP_t inCTP = EncodeCTP(tpcid.Cryostat, tpcid.TPC, plane);
      std::vector<std::vector<int>> tjList;
      TagShowerTjs(tjs, inCTP, tjList);
      if(tjList.empty()) continue;
      MergeTjList(tjList);
      bigList[plane] = tjList;
    } // plane
    unsigned short nPlanesWithShowers = 0;
    for(unsigned short plane = 0; plane < TPC.Nplanes(); ++plane) if(!bigList.empty()) ++nPlanesWithShowers;
    if(nPlanesWithShowers < 2) return false;
    
    for(unsigned short plane = 0; plane < TPC.Nplanes(); ++plane) {
      std::cout<<"Plane "<<plane<<" tjList \n";
      for(auto& tjl : bigList[plane]) {
        for(auto& tjID : tjl) {
          // mark them as InShower Tjs
          tjs.allTraj[tjID - 1].AlgMod[kInShower] = true;
          std::cout<<" "<<tjID;
        }
        std::cout<<"\n";
      } // tjl
    } // plane
    
    for(unsigned short plane = 0; plane < TPC.Nplanes(); ++plane) {
      CTP_t inCTP = EncodeCTP(tpcid.Cryostat, tpcid.TPC, plane);
      prt = (inCTP == dbgCTP || dbgPlane > 2);
      // Create a shower for each one
      for(auto& tjl : bigList[plane]) {
        unsigned short cotIndex = Create2DShower(tjs, tjl);
        if(cotIndex == USHRT_MAX) continue;
        DefineShower(tjs, cotIndex, prt);
        // Find nearby Tjs that were not included because they had too-high
        // MCSMom, etc. This will be used to decide if showers should be merged
        AddTjsInsideEnvelope(tjs, cotIndex, prt,1);
        FindNearbyTjs(tjs, cotIndex, prt);
        FindMatchingTjs(tjs, cotIndex, prt);
        if(prt) {
          ShowerStruct& ss = tjs.cots[cotIndex];
          Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
          PrintTrajectory("FS3D", tjs, stj, USHRT_MAX);
        }
      } // tjl
      // try to merge showers in this plane using the lists of nearby Tjs
      if(inCTP == UINT_MAX) continue;
      MergeOverlap(tjs, inCTP, prt);
      MergeSubShowers(tjs, inCTP, prt);
      MergeNearby2DShowers(tjs, inCTP, prt);
    } // plane
    
    prt = (dbgPlane > 2);
    Match2DShowers(tjs, tpcid, prt);
    
    // Look for a 3D-matched parent
    for(unsigned short cotIndex = 0; cotIndex < tjs.cots.size(); ++cotIndex) {
      if(tjs.cots[cotIndex].TjIDs.empty()) continue;
      prt = (tjs.cots[cotIndex].CTP == dbgCTP || dbgPlane > 2);
      FindExternalParent(tjs, cotIndex, prt);
      ShowerStruct& ss = tjs.cots[cotIndex];
      if(ss.TjIDs.empty()) continue;
      Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
      std::cout<<cotIndex<<" Pos "<<ss.CTP<<":"<<PrintPos(tjs, stj.Pts[1].Pos)<<" ParID "<<ss.ParentID<<" TruParID "<<ss.TruParentID<<"\n";
    } // cotIndex
    
    bool newShowers = false;
    for(unsigned short plane = 0; plane < TPC.Nplanes(); ++plane) {
      CTP_t inCTP = EncodeCTP(tpcid.Cryostat, tpcid.TPC, plane);
      prt = (plane == dbgPlane || dbgPlane > 2);
      CheckQuality(tjs, inCTP, prt);
      // Re-assign hits from the InShower Tjs to the ShowerTj.
      if(TransferTjHits(tjs, inCTP, prt)) newShowers = true;
    } // plane
    
    if(tjs.ShowerTag[12] >= 0) {
      for(auto& tj : tjs.allTraj) {
        if(tj.AlgMod[kKilled]) continue;
        if(!tj.AlgMod[kShowerTj]) continue;
        PrintTrajectory("FSO", tjs, tj, USHRT_MAX);
      }
    } // print trajectories
    
    return newShowers;
    
  } // FindShowers3D
  
  
  ////////////////////////////////////////////////
  void Match2DShowers(TjStuff& tjs, const geo::TPCID& tpcid, bool prt)
  {
        
    for(unsigned short ci1 = 0; ci1 < tjs.cots.size() - 1; ++ci1) {
      ShowerStruct& ss1 = tjs.cots[ci1];
      if(ss1.TjIDs.empty()) continue;
      for(unsigned short ci2 = ci1 + 1; ci2 < tjs.cots.size(); ++ci2) {
        ShowerStruct& ss2 = tjs.cots[ci2];
        if(ss2.CTP == ss1.CTP) continue;
        if(ss2.TjIDs.empty()) continue;
        // count the number of common Tjs
        std::vector<int> shared;
        std::set_intersection(ss1.MatchedTjIDs.begin(), ss1.MatchedTjIDs.end(), 
                              ss2.MatchedTjIDs.begin(), ss2.MatchedTjIDs.end(), std::back_inserter(shared));
        if(shared.empty()) continue;
        std::cout<<"M2DS: ss1 "<<ss1.CTP<<":"<<ci1<<" ss2 "<<ss2.CTP<<":"<<ci2<<" common "<<shared.size();
        float allcnt = 0;
        float matcnt = 0;
        // find all matched pairs
        for(auto& mtjID : ss1.MatchedTjIDs) allcnt += tjs.allTraj[mtjID - 1].Pts.size();
        for(auto& mtjID : ss2.MatchedTjIDs) allcnt += tjs.allTraj[mtjID - 1].Pts.size();
        std::cout<<" allcnt "<<allcnt;
        if(allcnt == 0) continue;
        for(auto& mtjID : shared) matcnt += tjs.allTraj[mtjID - 1].Pts.size();
        std::cout<<" matcnt "<<matcnt;
        float matfrac = 2 * matcnt / allcnt;
        std::cout<<" matfrac "<<matfrac;
        std::cout<<"\n";
      } // ci2
    } // ci1

  } // Match2DShowers

  ////////////////////////////////////////////////
  void FindMatchingTjs(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // fill the vector of Tjs in other planes that are matched to the Tjs in the shower
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    ss.MatchedTjIDs.clear();
    
    for(auto& tjID : ss.TjIDs) {
      unsigned int mvIndex = MatchVecIndex(tjs, tjID);
      if(mvIndex > tjs.matchVec.size() - 1) continue;
      for(auto& mtjID : tjs.matchVec[mvIndex].TjIDs) {
        // reference to the matching Tj
        Trajectory& mtj = tjs.allTraj[mtjID - 1];
        // not in the same plane
        if(mtj.CTP == ss.CTP) continue;
        if(std::find(ss.MatchedTjIDs.begin(), ss.MatchedTjIDs.end(), mtj.ID) == ss.MatchedTjIDs.end()) ss.MatchedTjIDs.push_back(mtj.ID);
      } // otjID
    } // tjID
    std::sort(ss.MatchedTjIDs.begin(), ss.MatchedTjIDs.end());

  } // FindMatchingTjs

 ////////////////////////////////////////////////
  void FindShowers(TjStuff& tjs, const CTP_t& inCTP)
  {
    // Construct clusters of trajectories (cots) which will become shower PFParticles
    
    
    /*
     # 0 Mode (<= 0 OFF, 1 = find showers before 3D match, 2 = find showers after 3D match)
     # 1 Max Tj MCSMom for a shower tag
     # 2 Max separation
     # 3 Min energy (MeV)
     # 4 rms width factor
     # 5 Min shower 1/2 width (WSE units)
     # 6 Min total Tj Pts
     # 7 Min Tjs
     # 8 max parent FOM
     # 9 max direction FOM
     # 10 max aspect ratio
     # 11 min Score to preserve a vertex and its Tjs
     # 12 Debug in CTP (>10 debug cotIndex + 10)
     */
    
    if(tjs.ShowerTag[0] != 1) return;
    
    bool prt = false;
    // print only one shower?
    unsigned short prtShower = USHRT_MAX;
    if(tjs.ShowerTag[12] >= 0) {
      geo::PlaneID planeID = DecodeCTP(inCTP);
      CTP_t printCTP = EncodeCTP(planeID.Cryostat, planeID.TPC, std::nearbyint(tjs.ShowerTag[12]));
      prt = (printCTP == inCTP);
      if(printCTP > 2) prt = true;
      if(printCTP > 9) prtShower = printCTP - 10;
    }
    // save the requested print state in case it gets changed
    bool saveprt = prt;
    /*
    // temp for testing
    if(inCTP == 0 && !tjs.MCPartList.empty()) {
      // Print some info on the first primary particle (electron)
      const simb::MCParticle* part = tjs.MCPartList[0];
      std::cout<<"Primary E = "<<std::setprecision(2)<<part->E();
      TVector3 dir;
      dir[0] = part->Px(); dir[1] = part->Py(); dir[2] = part->Pz();
      if(dir.Mag() != 0) dir.SetMag(1);
      std::cout<<" dir "<<std::setprecision(2)<<dir[0]<<" "<<dir[1]<<" "<<dir[2]<<"\n";
      for(CTP_t ctp = 0; ctp < 3; ++ctp) {
        std::cout<<" CTP "<<ctp;
        // print the primary trajectory ID
        std::cout<<" primary TjID "<<MCParticleStartTjID(tjs, 0, ctp);
        TrajPoint tp;
        tp.CTP = ctp;
        MakeTruTrajPoint(tjs, 0, tp);
        std::cout<<" Tru start pos "<<PrintPos(tjs, tp.Pos)<<" Ang "<<tp.Ang<<" Projection in plane "<<tp.Delta;
        std::cout<<"\n";
      } // ctp
    } // temp testing
    */

    std::vector<std::vector<int>> tjList;
    TagShowerTjs(tjs, inCTP, tjList);
    
    // save stage 1 info
    if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, tjList, 1);
    
    if(prt) std::cout<<"Inside FindShowers inCTP "<<inCTP<<" tjList size "<<tjList.size()<<"\n";
    if(tjList.empty()) return;
    MergeTjList(tjList);

    // save stage 2 info
    if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, tjList, 2);

    if(prt) {
      mf::LogVerbatim myprt("TC");
      myprt<<"tjlist\n";
      for(auto& tjl : tjList) {
        if(tjl.empty()) continue;
        for(auto& tjID : tjl) myprt<<" "<<tjID<<"_"<<tjs.allTraj[tjID-1].NNeighbors;
        myprt<<"\n";
      } // tjl
    } // prt


    //MergeTjList2(tjs, tjList, prt);
    
    // remove Tjs that don't have enough neighbors = ShowerTag[7] unless the shower
    // has few Tjs
    unsigned short minNeighbors = tjs.ShowerTag[7];
    if(minNeighbors > 0) --minNeighbors;
    for(auto& tjl : tjList) {
      bool didErase = true;
      while(didErase) {
        didErase = false;
        unsigned short indx = 0;
        for(indx = 0; indx < tjl.size(); ++indx) {
          unsigned short itj = tjl[indx] - 1;
          if(tjl.size() > 5 && tjs.allTraj[itj].NNeighbors < minNeighbors) break;
        } // indx
        if(indx < tjl.size()) {
          tjl.erase(tjl.begin() + indx);
          didErase = true;
        }
      } // didErase
    } // tjl
    
    // save stage 3 info                                                                            
    if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, tjList, 3);

    // mark all of these as InShower Tjs
    for(auto& tjl : tjList) {
      for(auto& tjID : tjl) {
        tjs.allTraj[tjID - 1].AlgMod[kInShower] = true;
      } // tjID
    } // tjl

    // Convert each one into a shower with a shower Tj
    for(auto& tjl : tjList) {
      if(tjl.empty()) continue;
      // Create the shower Tj
      unsigned short cotIndex = Create2DShower(tjs, tjl);
      if(cotIndex == USHRT_MAX) continue;
      ShowerStruct& ss = tjs.cots[cotIndex];
      Trajectory& stj = tjs.allTraj[ss.ShowerTjID-1];
      if(prt && prtShower != USHRT_MAX) prt = (cotIndex == prtShower);
      if(prt) {
        mf::LogVerbatim myprt("TC");
        myprt<<"Make cots "<<cotIndex<<" in CTP "<<ss.CTP<<" TjID_NN";
        for(auto& tjID : tjl) myprt<<" "<<tjID<<"_"<<tjs.allTraj[tjID-1].NNeighbors;
      }
      DefineShower(tjs, cotIndex, prt);
      if(tjs.cots[cotIndex].TjIDs.empty()) continue;

      if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, cotIndex, 4);
      // Fill the vector of Tjs that are close to this shower but were not included in it, most
      // likely because the MCSMom is too high. These will be used to merge showers
      // FindNearbyTjs(tjs, cotIndex, prt);
      // Try to add more Tjs to the shower

      AddTjsInsideEnvelope(tjs, cotIndex, prt,1);
      if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, cotIndex, 5);

      FindExternalParent(tjs, cotIndex, prt);

      if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, cotIndex, 6);
      // If no external parent was found, try to refine the direction and look for
      // an internal parent
      if(tjs.cots[cotIndex].ShowerTjID == 0) RefineShowerTj(tjs, cotIndex, prt);
      if(prt) PrintTrajectory("FS", tjs, tjs.allTraj[stj.ID-1], USHRT_MAX);



    } // tjl
    
    if(tjs.cots.empty()) return;

    prt = saveprt;
    // Merge showers whose envelopes overlap
    MergeOverlap(tjs, inCTP, prt);
    // Merge small showers with larger ones
    MergeSubShowers(tjs, inCTP, prt);
    
    // drop those that don't meet the requirements
    for(unsigned short cotIndex = 0; cotIndex < tjs.cots.size(); ++cotIndex) {
      ShowerStruct& ss = tjs.cots[cotIndex];
      if(ss.CTP != inCTP) continue;
      if(ss.TjIDs.empty()) continue;
      // enough Tjs?
      unsigned short ntjs = ss.TjIDs.size();
      bool killit = (ntjs < tjs.ShowerTag[7]);
      // Kill runt showers
      if(prt && prtShower != USHRT_MAX) prt = (cotIndex == prtShower);
      if(!killit) killit = (ss.Energy < tjs.ShowerTag[3]);
      if(prt) mf::LogVerbatim("TC")<<"cotIndex "<<cotIndex<<" nTjs "<<ss.TjIDs.size()<<" nTjs "<<ss.TjIDs.size()<<" killit? "<<killit;
      if(!killit) {
        // count the number of Tj points
        unsigned short nTjPts = 0;
        for(auto& tjID : ss.TjIDs) {
          Trajectory& tj = tjs.allTraj[tjID - 1];
          nTjPts += NumPtsWithCharge(tjs, tj, false);
        }  // tjID
        if(nTjPts < tjs.ShowerTag[6]) killit = true;
        if(prt) mf::LogVerbatim("TC")<<"    "<<" nTjPts "<<nTjPts<<" killit? "<<killit;
      } // !killit
      if(killit) {
        MakeShowerObsolete(tjs, cotIndex, prt);
      } else {
        if(tjs.allTraj[ss.ShowerTjID - 1].AlgMod[kKilled]) {
          std::cout<<"FS logic error: ShowerTj "<<tjs.allTraj[ss.ShowerTjID - 1].ID<<" is killed\n";
          tjs.allTraj[ss.ShowerTjID - 1].AlgMod[kKilled] = false;
        }
        // A good shower. Set the pdgcode of InShower Tjs to 11
        for(auto& tjID : ss.TjIDs) {
          Trajectory& tj = tjs.allTraj[tjID - 1];
          tj.PDGCode = 11;
          // Clobber 2D vertices that are inside the shower
          for(unsigned short end = 0; end < 2; ++end) {
            if(tj.VtxID[end] > 0) {
              VtxStore& vx2 = tjs.vtx[tj.VtxID[end]-1];
              bool killMe = (vx2.Score < tjs.ShowerTag[11]);
              // don't kill the 2D vertex if it is attached to the far end of the parent Tj
              if(killMe && ss.ParentID == tjID) {
                unsigned short farEnd = FarEnd(tjs, tj, ss);
                if(farEnd == end) killMe = false;
              }
              if(killMe) {
                if(prt) mf::LogVerbatim("TC")<<"Clobber vtx "<<tj.VtxID[end]<<" Score "<<vx2.Score<<" Vtx3ID "<<tjs.vtx[tj.VtxID[end]-1].Vtx3ID;
                MakeVertexObsolete(tjs, tj.VtxID[end]);
              }
            }
          } // end
        }
      } // don't killit

      if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, cotIndex, 7);

    } // ic

    CheckQuality(tjs, inCTP, prt);

    
    // find the start charge
    for(unsigned short cotIndex = 0; cotIndex < tjs.cots.size(); ++cotIndex) {
      ShowerStruct& ss = tjs.cots[cotIndex];
      if(ss.CTP != inCTP) continue;
      if(ss.TjIDs.empty()) continue;
      FindStartChg(tjs, cotIndex, prt);
      if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, cotIndex, 8);
    }
    
    // Finish up in this CTP. 
    // Re-assign hits from the InShower Tjs to the ShowerTj.
    TransferTjHits(tjs, inCTP, prt);
    std::cout<<"Final calculation shower energy...\n";

    // check for consistency
    unsigned short cotIndex = 0; 
    for(auto& ss : tjs.cots) {
      if(ss.TjIDs.empty()) continue;
      if(ss.CTP != inCTP) continue;
      if(ss.ShowerTjID == 0) {
        std::cout<<"FindShowers: ShowerTjID not defined in CTP "<<ss.CTP<<"\n";
      }
      for(auto& tjID : ss.TjIDs) {
        if(tjID > (int)tjs.allTraj.size()) {
          std::cout<<"FindShowers: Bad tjID "<<tjID<<"\n";
        }
        Trajectory& tj = tjs.allTraj[tjID - 1];
        if(tj.CTP != ss.CTP) {
          std::cout<<"FindShowers: Bad CTP "<<ss.CTP<<" "<<tj.CTP<<" tjID "<<tjID<<"\n";
        }
        if(!tj.AlgMod[kKilled] || !tj.AlgMod[kInShower]) {
          std::cout<<"FindShowers: InShower TjID "<<tjID<<" invalid kKilled "<<tj.AlgMod[kKilled]<<" or kInShower "<<tj.AlgMod[kInShower]<<"\n";
        }
      } // tjID

      // need write code to do this after TransferTjHits
      // has been called. Turning off for now
      //      if (tjs.SaveShowerTree) SaveTjInfo(tjs, inCTP, cotIndex, 9);

      cotIndex++;
    } // ss
    
    if(tjs.ShowerTag[12] >= 0) {
      for(unsigned short ic = 0; ic < tjs.cots.size(); ++ic) {
        if(tjs.cots[ic].TjIDs.empty()) continue;
        unsigned short itj = tjs.cots[ic].ShowerTjID - 1;
        Trajectory& tj = tjs.allTraj[itj];
        if(prt || (tjs.ShowerTag[12] == 3 && tj.CTP == inCTP)) PrintTrajectory("FSO", tjs, tj, USHRT_MAX);
      } // ic
    } // print trajectories
    
    if(tjs.ShowerTag[12] >= 100) {
      unsigned short ic = tjs.ShowerTag[12] - 100;
      if(ic < tjs.cots.size() && tjs.cots[ic].CTP == inCTP) DumpShowerPts(tjs, ic);
    }
    
  } // FindShowers
  
  ////////////////////////////////////////////////
  void MergeTjList(std::vector<std::vector<int>>& tjList)
  {
    // Merge the lists of Tjs in the lists if they share a common Tj ID
    bool didMerge = true;
    while(didMerge) {
      didMerge = false;
      for(unsigned short itl = 0; itl < tjList.size() - 1; ++itl) {
        if(tjList[itl].empty()) continue;
        for(unsigned short jtl = itl + 1; jtl < tjList.size(); ++jtl) {
          if(tjList[itl].empty()) continue;
          auto& itList = tjList[itl];
          auto& jtList = tjList[jtl];
          // See if the j Tj is in the i tjList
          bool jtjInItjList = false;
          for(auto& jtj : jtList) {
            if(std::find(itList.begin(), itList.end(), jtj) != itList.end()) {
              jtjInItjList = true;
              break;
            }
            if(jtjInItjList) break;
          } // jtj
          if(jtjInItjList) {
            // append the jtList to itList
            itList.insert(itList.end(), jtList.begin(), jtList.end());
            // clear jtList
            jtList.clear();
            didMerge = true;
          }
        } // jtl
      } // itl
    } // didMerge
    
    // erase the deleted elements
    unsigned short imEmpty = 0;
    while(imEmpty < tjList.size()) {
      for(imEmpty = 0; imEmpty < tjList.size(); ++imEmpty) if(tjList[imEmpty].empty()) break;
      if(imEmpty < tjList.size()) tjList.erase(tjList.begin() + imEmpty);
    } // imEmpty < tjList.size()
    
    // sort the lists by increasing ID and remove duplicates
    for(auto& tjl : tjList) {
      std::sort(tjl.begin(), tjl.end());
      auto last = std::unique(tjl.begin(), tjl.end());
      tjl.erase(last, tjl.end());
    } // tjl
    
  } // MergeTjList

  ////////////////////////////////////////////////
  void FillPts(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    
    unsigned int cnt = 0;
    for(unsigned short it = 0; it < ss.TjIDs.size(); ++it) {
      unsigned short itj = ss.TjIDs[it] - 1;
      if(itj > tjs.allTraj.size() - 1) {
        mf::LogWarning("TC")<<"Bad TjID "<<ss.TjIDs[it];
        MakeShowerObsolete(tjs, cotIndex, prt);
        return;
      }
      Trajectory& tj = tjs.allTraj[itj];
      if(tj.CTP != ss.CTP) {
        mf::LogWarning("TC")<<"Tj "<<tj.ID<<" is in the wrong CTP "<<tj.CTP<<" "<<ss.CTP;
        MakeShowerObsolete(tjs, cotIndex, prt);
        return;
      }
      if(tj.AlgMod[kShowerTj]) {
        mf::LogWarning("TC")<<"DSTj: Tj "<<tj.ID<<" is in TjIDs in cotIndex "<<cotIndex<<" but is a ShowerTj! Killing it";
        MakeShowerObsolete(tjs, cotIndex, prt);
        return;
      }
      for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) {
        TrajPoint& tp = tj.Pts[ipt];
        for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) if(tp.UseHit[ii]) ++cnt;
//        if(tp.Chg > 0) ++cnt;
      } // ipt
    } // it
    
    // Add any loose hits (those not in trajectory points) that are stashed in shower Tj Pt[0]
    TrajPoint& stp0 = tjs.allTraj[ss.ShowerTjID - 1].Pts[0];
    cnt += stp0.Hits.size();
    
    ss.Pts.resize(cnt);
    
    // Now populate the vectors with the information we currently have 
    cnt = 0;
    float totChg = 0;
    for(unsigned short it = 0; it < ss.TjIDs.size(); ++it) {
      Trajectory& tj = tjs.allTraj[ss.TjIDs[it] - 1];
      for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) {
        TrajPoint& tp = tj.Pts[ipt];
        if(tp.Chg <= 0) continue;
        // create a point for every hit
        for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
          if(tp.UseHit[ii]) {
            unsigned int iht = tp.Hits[ii];
            ss.Pts[cnt].HitIndex = iht;
            ss.Pts[cnt].TID = tj.ID;
            ss.Pts[cnt].Chg = tjs.fHits[iht].Integral;
            ss.Pts[cnt].Pos[0] = tjs.fHits[iht].WireID.Wire;
            ss.Pts[cnt].Pos[1] = tjs.fHits[iht].PeakTime * tjs.UnitsPerTick;
            totChg += ss.Pts[cnt].Chg;
            ++cnt;
          }
        } // ii
      } // ipt
    } // it
    
    // include the loose hits store in stp0.Hits
    for(auto& iht : stp0.Hits) {
      ss.Pts[cnt].HitIndex = iht;
      ss.Pts[cnt].TID = tjs.fHits[iht].InTraj;
      ss.Pts[cnt].Pos[0] = tjs.fHits[iht].WireID.Wire;
      ss.Pts[cnt].Pos[1] = tjs.fHits[iht].PeakTime * tjs.UnitsPerTick;
      ss.Pts[cnt].Chg = tjs.fHits[iht].Integral;
      totChg += ss.Pts[cnt].Chg;
      ++cnt;
    }
    
    // Put the total charge into the shower Tj
    tjs.allTraj[ss.ShowerTjID - 1].AveChg = totChg;
    if(prt) mf::LogVerbatim("TC'")<<"FP: cotIndex "<<cotIndex<<" filled "<<cnt<<" points including "<<stp0.Hits.size()<<" loose hits. Total charge "<<(int)totChg;
    
  } // FillPts

  ////////////////////////////////////////////////
  void DefineShower(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Defines the properties of a shower using the trajectory points within the trajectories listed
    // in TjIDs. This wipes out any other information that may exist
    
    FillPts(tjs, cotIndex, prt);
    if(!FindChargeCenter(tjs, cotIndex, prt)) {
      mf::LogWarning("TC")<<"Failed to find shower charge center";
      return;
    }
    FindAngle(tjs, cotIndex, prt);
    FillRotPos(tjs, cotIndex, prt);
    if(!DefineShowerTj(tjs, cotIndex, prt)) {
      if(prt) mf::LogVerbatim("TC")<<"Failed to define Shower Tj. Killed the shower";
      MakeShowerObsolete(tjs, cotIndex, prt);
      return;
    }
    FindNearbyTjs(tjs, cotIndex, prt);
    DefineEnvelope(tjs, cotIndex, prt);

  } // DefineShower
  
  ////////////////////////////////////////////////
  bool AddTj(TjStuff& tjs, unsigned short tjID, const unsigned short& cotIndex, bool doUpdate, bool prt)
  {
    // Adds the Tj to the shower and optionally updates the shower variables
    
    if(tjID > tjs.allTraj.size()) return false;
    if(cotIndex > tjs.cots.size() - 1) return false;
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.ShowerTjID == 0) return false;
    Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];

    // make sure it isn't already in a shower
    Trajectory& tj = tjs.allTraj[tjID - 1];
    if(tj.AlgMod[kInShower]) {
      mf::LogVerbatim("TC")<<"AddTj: Tj "<<tjID<<" is already an InShower Tj";
      return false;
    }
    if(std::find(ss.TjIDs.begin(), ss.TjIDs.end(), tjID) != ss.TjIDs.end()) {
      mf::LogVerbatim("TC")<<"AddTj: Tj "<<tjID<<" is already in this shower "<<cotIndex;
      return false;
    }
    if(tj.CTP != ss.CTP) {
      mf::LogVerbatim("TC")<<"AddTj: Tj "<<tjID<<" is in the wrong CTP "<<cotIndex;
      return false;
    }
    // don't add a Tj to the shower if it has a nice vertex unless it is the parent
    if(ss.ParentID != tjID && TjHasNiceVtx(tjs, tj, (unsigned short)tjs.ShowerTag[11])) {
      mf::LogVerbatim("TC")<<"AddTj: Tj "<<tjID<<" in cotIndex "<<cotIndex<<" has a nice vertex. Not adding it";
      return false;
    }
    // Ensure that we are not trying to add a Tj that is attached to the shower Tj vertex
    if(stj.VtxID[0] > 0) {
      // Get a list of TjIDs that are attached to the shower Tj vertex
      auto vxTjIDs = GetVtxTjIDs(tjs, tjs.vtx[stj.VtxID[0] - 1]);
      // make sure this Tj ID isn't in the list
      if(std::find(vxTjIDs.begin(), vxTjIDs.end(), tjID) != vxTjIDs.end()) return false;
    }
    ss.TjIDs.push_back(tjID);
    std::sort(ss.TjIDs.begin(), ss.TjIDs.end());
    // count the hits in the TPs
    unsigned short cnt = 0;
    for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) {
      TrajPoint& tp = tj.Pts[ipt];
      if(tp.Chg == 0) continue;
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) if(tp.UseHit[ii]) ++cnt;
    } // ipt
    unsigned short newSize = ss.Pts.size() + cnt;
    cnt = ss.Pts.size();
    ss.Pts.resize(newSize);
    // now add them
    for(unsigned short ipt = tj.EndPt[0]; ipt <= tj.EndPt[1]; ++ipt) {
      TrajPoint& tp = tj.Pts[ipt];
      if(tp.Chg == 0) continue;
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
        if(tp.UseHit[ii]) {
          unsigned int iht = tp.Hits[ii];
          ss.Pts[cnt].HitIndex = iht;
          ss.Pts[cnt].TID = tj.ID;
          ss.Pts[cnt].Chg = tjs.fHits[iht].Integral;
          ss.Pts[cnt].Pos[0] = tjs.fHits[iht].WireID.Wire;
          ss.Pts[cnt].Pos[1] = tjs.fHits[iht].PeakTime * tjs.UnitsPerTick;
          ++cnt;
        }
      }
    } // ipt
    tj.AlgMod[kInShower] = true;
    if(prt) mf::LogVerbatim("TC")<<"ATj: Add Tj "<<tj.ID;
    
    if(doUpdate) DefineShower(tjs, cotIndex, prt);
    
    return true;
  } // AddTj
  
  ////////////////////////////////////////////////
  bool RemoveTj(TjStuff& tjs, unsigned short TjID, const unsigned short& cotIndex, bool doUpdate, bool prt)
  {
    // Removes the Tj from a shower
    
    if(TjID > tjs.allTraj.size()) return false;
    if(cotIndex > tjs.cots.size() - 1) return false;
    
    // make sure it isn't already in a shower
    Trajectory& tj = tjs.allTraj[TjID - 1];
    if(!tj.AlgMod[kInShower]) return false;
    tj.AlgMod[kInShower] = false;
    ShowerStruct& ss = tjs.cots[cotIndex];
    bool gotit = false;
    for(unsigned short ii = 0; ii < ss.TjIDs.size(); ++ii) {
      if(TjID == ss.TjIDs[ii]) {
        ss.TjIDs.erase(ss.TjIDs.begin() + ii);
        gotit = true;
        break;
      }
    } // ii
    if(!gotit) return false;
    // Removing a parent Tj?
    if(TjID == ss.ParentID) ss.ParentID = 0;
    // re-build everything?
    if(prt) mf::LogVerbatim("TC")<<"RemoveTj "<<TjID<<" from cotIndex "<<cotIndex;
    if(doUpdate) DefineShower(tjs, cotIndex, prt);
    return true;
  } // RemoveTj

  ////////////////////////////////////////////////
  void FindExternalParent(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Look for a parent trajectory that starts outside the shower and ends inside.

    /*
     # 0 Mode (<= 0 OFF, 1 = find showers before 3D match, 2 = find showers after 3D match)
     # 1 Max Tj MCSMom for a shower tag
     # 2 Max separation
     # 3 Min energy (MeV)
     # 4 rms width factor
     # 5 Min shower 1/2 width (WSE units)
     # 6 Min total Tj Pts
     # 7 Min Tjs
     # 8 max parent FOM
     # 9 max direction FOM
     # 10 max aspect ratio
     # 11 Debug in CTP (>10 debug cotIndex + 10)
     */

    if(cotIndex > tjs.cots.size() - 1) return;
    ShowerStruct& ss = tjs.cots[cotIndex];
    // Ensure that it is valid
    if(ss.TjIDs.empty()) return;
    if(ss.Envelope.empty()) return;
    
    // require 3D matched parent?
    bool require3DMatch = (tjs.ShowerTag[0] == 2);
    
    // References to shower Tj points
    TrajPoint& stp0 = tjs.allTraj[ss.ShowerTjID - 1].Pts[0];
    TrajPoint& stp1 = tjs.allTraj[ss.ShowerTjID - 1].Pts[1];
    unsigned short oldParent = ss.ParentID;
    if(prt) {
      mf::LogVerbatim myprt("TC");
      myprt<<"FEP: cotIndex "<<cotIndex<<" stj.ID "<<tjs.allTraj[ss.ShowerTjID - 1].ID<<" Existing parent ID "<<oldParent<<" parent FOM "<<ss.ParentFOM;
      myprt<<" attached to vertex "<<tjs.allTraj[ss.ShowerTjID - 1].VtxID[0];
      myprt<<" Tjs";
      for(auto& tid : ss.TjIDs) myprt<<" "<<tid;
    }
    
    if(ss.AspectRatio > tjs.ShowerTag[10] || ss.DirectionFOM > tjs.ShowerTag[9]) {
      if(prt) mf::LogVerbatim("TC")<<" Don't search for a parent due to poor AspectRatio "<<ss.AspectRatio<<" or ss.DirectionFOM "<<ss.DirectionFOM;
      return;
    }
    // detach the showerTj from a vertex
    tjs.allTraj[ss.ShowerTjID - 1].VtxID[0] = 0;
    
    float bestFOM = ss.ParentFOM;
    unsigned short imTheBest = USHRT_MAX;
    unsigned short imTheBestPt = 0;
    for(auto& tj : tjs.allTraj) {
      if(tj.CTP != ss.CTP) continue;
      if(tj.AlgMod[kKilled] && !tj.AlgMod[kInShower]) continue;
      // ignore shower Tjs
      if(tj.AlgMod[kShowerTj]) continue;
      // ignore in-shower Tjs that aren't in this shower
      if(tj.AlgMod[kInShower] && std::find(ss.TjIDs.begin(), ss.TjIDs.end(), tj.ID) == ss.TjIDs.end()) continue;
      // Ignore short Tjs
      if(tj.Pts.size() < 5) continue;
      if(require3DMatch && !tj.AlgMod[kMat3D]) continue;
      // find the point that is farthest from stp1
      float sep0 = PosSep2(tj.Pts[tj.EndPt[0]].Pos, stp1.Pos);
      float sep1 = PosSep2(tj.Pts[tj.EndPt[1]].Pos, stp1.Pos);
      unsigned short useEnd = 0;
      if(sep1 > sep0) useEnd = 1;
      // Check trajectories that were split by 3D vertex matching
      if(WrongSplitTj(tjs, tj, useEnd, ss, prt)) continue;
      float fom = ParentFOM(tjs, tj, useEnd, ss, prt);
      if(fom > bestFOM) continue;
      bestFOM = fom;
      imTheBest = tj.ID;
      imTheBestPt = tj.EndPt[useEnd];
    } // tj

    if(imTheBest == USHRT_MAX) return;
    if(bestFOM > tjs.ShowerTag[8]) {
      if(prt) mf::LogVerbatim("TC")<<"FEP: Best parent candidate FOM "<<bestFOM<<" exceeds the cut "<<tjs.ShowerTag[8];
      return;
    }
    
    ss.ParentID = imTheBest;
    ss.ParentFOM = bestFOM;
    // Move the shower start point to the parent
    TrajPoint& ptp = tjs.allTraj[imTheBest - 1].Pts[imTheBestPt];
    stp0.Pos = ptp.Pos;
    // Set the shower angle to the parent angle. Ensure that they are in the same direction
    double parAngle = ptp.Ang;
    if(std::abs(parAngle - ss.Angle) > M_PI/2) {
      // Need to flip ptp.Ang by pi
      if(parAngle > 0) {
        parAngle -= M_PI;
      } else {
        parAngle += M_PI;
      }
    } // angle consistency check
    ss.Angle = parAngle;
    double cs = cos(ptp.Ang);
    double sn = sin(ptp.Ang);
    // move all the points onto the parent trajectory
    for(auto& tp : tjs.allTraj[ss.ShowerTjID - 1].Pts) {
      double dist = PosSep(ptp.Pos, tp.Pos);
      tp.Pos[0] = ptp.Pos[0] + cs * dist;
      tp.Pos[1] = ptp.Pos[1] + sn * dist;
    } // tp
    
    if(imTheBest != oldParent) {
      Trajectory& tj = tjs.allTraj[imTheBest-1];
      if(std::find(ss.TjIDs.begin(), ss.TjIDs.end(), tj.ID) == ss.TjIDs.end()) {
        if(prt) mf::LogVerbatim("TC")<<"FEP: Adding new parent "<<imTheBest<<" to the shower with FOM = "<<bestFOM<<" stp1 Pos "<<PrintPos(tjs, stp1);
        // Add the Tj to the shower and re-define it
        if(!AddTj(tjs, imTheBest, cotIndex, false, prt)) {
          if(prt) mf::LogVerbatim("TC")<<" Failed to add the parent for some reason. Recovering...";
          ss.ParentID = 0;
          ss.ParentFOM = 999;
          return;
        }
      } else {
        if(prt) mf::LogVerbatim("TC")<<"FEP: Existing InShower Tj "<<imTheBest<<" promoted to parent status with FOM = "<<bestFOM<<" stp1 Pos "<<PrintPos(tjs, stp1);
      }
      ss.NeedsUpdate = true;
      DefineShower(tjs, cotIndex, prt);
      AddTjsInsideEnvelope(tjs, cotIndex, prt,2);
    } else {
      if(prt) mf::LogVerbatim("TC")<<"FEP: Existing parent is good ";
      ss.NeedsUpdate = false;
    }
    
    if(!ss.NeedsUpdate) return;
    if(ss.ParentID == 0) return;
    
    // see if the new parent is attached to a vertex
    Trajectory& ptj = tjs.allTraj[ss.ParentID-1];
    // determine the apprropriate start end of the parent
    unsigned short end = FarEnd(tjs, ptj, ss);
    if(end != 0) {
      std::cout<<"FEP: Parent end is not 0... Is this bad?\n";
    }
    Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
    stj.VtxID[0] = ptj.VtxID[end];
    // ensure that any Tjs attached to this vertex aren't included in the shower (except for
    // the parent)
    if(stj.VtxID[0] > 0) {
      auto vtxTjIDs = GetVtxTjIDs(tjs, tjs.vtx[stj.VtxID[0]-1]);
      std::vector<int> shared;
      std::set_intersection(vtxTjIDs.begin(), vtxTjIDs.end(),
                            ss.TjIDs.begin(), ss.TjIDs.end(), std::back_inserter(shared));
      for(auto& tjID : shared) {
        if(tjID == ss.ParentID) continue;
        // don't bother re-defining the shower since it will be done later
        RemoveTj(tjs, tjID, cotIndex, false, prt);
      } // tjID
    } // stj.VtxID[0] > 0
    if(prt) mf::LogVerbatim("TC")<<" Set ShowerTj VtxID[0] "<<stj.VtxID[0];
    
  } // FindExternalParent

  ////////////////////////////////////////////////
  unsigned short FarEnd(TjStuff& tjs, const Trajectory& tj, ShowerStruct& ss)
  {
    // Returns the end (0 or 1) of the Tj that is furthest away from the shower center
    if(ss.ShowerTjID == 0) return 0;
    TrajPoint& stp1 = tjs.allTraj[ss.ShowerTjID-1].Pts[1];
    unsigned short endPt0 = tj.EndPt[0];
    unsigned short endPt1 = tj.EndPt[1];
    if(PosSep2(tj.Pts[endPt1].Pos, stp1.Pos) > PosSep2(tj.Pts[endPt0].Pos, stp1.Pos)) return 1;
    return 0;
  } // FarEnd

  ////////////////////////////////////////////////
  float ParentFOM(TjStuff& tjs, Trajectory& tj, const unsigned short& tjEnd, ShowerStruct& ss, bool prt)
  {
    // returns a FOM for the trajectory at the end point being the parent of ss
    
    if(tjEnd > 1) return 1000;
    if(ss.Energy == 0) return 1000;
    
    // Radiation length converted to WSE units (for uB)
    constexpr float radLen = 14 / 0.3;
    constexpr float tenRadLen2 = 100 * radLen * radLen;

    if(ss.TjIDs.empty()) return 1000;
    if(ss.ShowerTjID == 0) return 1000;
    
    // prospective parent TP
    unsigned short endPt = tj.EndPt[tjEnd];
    TrajPoint& ptp = tj.Pts[endPt];
    // Shower charge center TP
    unsigned short istj = ss.ShowerTjID - 1;
    TrajPoint& stp1 = tjs.allTraj[istj].Pts[1];
    float tp1Sep = PosSep2(ptp.Pos, stp1.Pos);
    // Make a rough cut on radiation lengths
    if(tp1Sep > tenRadLen2) {
      if(prt) mf::LogVerbatim("TC")<<"PFOM "<<tj.ID<<" failed sep cut "<<(int)sqrt(tp1Sep)<<" 10 radiation lengths "<<(int)sqrt(tenRadLen2);
      return 100;
    }
    tp1Sep = sqrt(tp1Sep);
    
    // impact parameter between the projection of ptp and the charge center
    float delta = PointTrajDOCA(tjs, stp1.HitPos[0], stp1.HitPos[1], ptp);
    // make a rough cut
    if(delta > 100) {
      if(prt) mf::LogVerbatim("TC")<<"PFOM "<<tj.ID<<" failed delta cut "<<delta<<" cut = 100";
      return 50;
    }
    
    // Estimate shower max. This parameterization comes from an Excel spreadsheet that uses the PDG shower max parameterization
    // from EGS4. Shower max, tmax, is calculated and used to generate a table of dE/dt vs t, which is then summed.
    // The value of t which yields 90% energy containment is used as a bound to find the shower center which is where the
    // shower center stp1 should be relative to the start of the parent. The parameterization is in units of the number of
    // radiation lengths with shEnergy in MeV.
    // Here is a summary table
    // E    t
    //----------
    //  70   1.90
    // 100   2.34
    // 150   2.68
    // 200   2.93
    // 250   3.10
    // 300   3.26
    // 350   3.34
    // 400   3.47
    // 600   3.79
    // 800   4.00
    //1000   4.11
    // Expected separation (cm) between the start of the parent trajectory and the shower charge center
    float expectedTPSep = 0.85 * log(3 * ss.Energy) - 2.65;
    // Convert it to the distance in WSE units 
    // We don't need great accuracy here because we don't know the projection of the shower in this view
    expectedTPSep *= radLen;
    // Assume that the projection of the shower in this view will be ~2/3
    expectedTPSep *= 0.6;
    // Guess that the RMS of the separation will be ~50% of the separation
    float expectedTPSepRMS = 0.8 * expectedTPSep;
    float sepPull = (tp1Sep - expectedTPSep) / expectedTPSepRMS;
    // The error on delta is probably dominated by not getting all of the shower Tjs and hits included
    // + missing energy (photons, etc). Just use the shower width at the shower center 
    float deltaErr = tjs.allTraj[istj].Pts[1].DeltaRMS;
    // protect against errors
    if(deltaErr < 0) deltaErr = 1;
    float deltaPull = delta / deltaErr;
    float dang = DeltaAngle(ptp.Ang, stp1.Ang);
    float dangErr = ss.AngleErr;
    if(dangErr < 0.1) dangErr = 0.1;
    // weight by the direction FOM?
    dangErr *= ss.DirectionFOM;
    float dangPull = dang / dangErr;
    float mom = tj.MCSMom;
    if(mom > 500) mom = 500;
    float momPull = (mom - 500) / 100;
    // Pull due to the minimum separation between the other end of the parent Tj and the first shower Tj point.
    float tp0Sep2 = 0;
    float sep0Pull2 = 0;
    unsigned short otherEndPt = tj.EndPt[1-tjEnd];
    TrajPoint& optp = tj.Pts[otherEndPt];
    TrajPoint& stp0 = tjs.allTraj[istj].Pts[0];
    // don't bother taking the sqrt since it would be squared in a few lines anyway
    tp0Sep2 = PosSep2(optp.Pos, stp0.Pos);
    // expect this to be 1/2 of the separation between shower Tj point 0 and point 1
    float expectTp0Sep = 0.25 * PosSep2(stp0.Pos, stp1.Pos);
    sep0Pull2 = tp0Sep2 / expectTp0Sep;
    // primary Tj length
    float lenPull = (TrajLength(tj) - expectedTPSep) / expectedTPSepRMS;
    float fom = sqrt(sepPull * sepPull + deltaPull * deltaPull + dangPull * dangPull + momPull * momPull + sep0Pull2 + lenPull * lenPull);
    fom /= 6;
    if(prt) {
      mf::LogVerbatim myprt("TC");
      myprt<<"PFOM: Tj "<<tj.ID<<" Pos "<<PrintPos(tjs, ptp);
      myprt<<" VtxID "<<tj.VtxID[tjEnd];
      if(tj.VtxID[tjEnd] > 0) {
        VtxStore& vx2 = tjs.vtx[tj.VtxID[tjEnd-1]];
        myprt<<" Vtx3ID "<<vx2.Vtx3ID;
      }
      myprt<<std::fixed<<std::setprecision(2);
      myprt<<" tp1Sep "<<tp1Sep<<" pull "<<sepPull;
      myprt<<" delta "<<delta<<" pull "<<deltaPull;
      myprt<<" dang "<<dang<<" pull "<<dangPull;
      myprt<<" mcsmom "<<(int)mom<<" pull "<<momPull;
      myprt<<" sep0 "<<sqrt(tp0Sep2)<<" pull "<<sqrt(sep0Pull2);
      myprt<<" length "<<(int)TrajLength(tj)<<" pull "<<lenPull;
      myprt<<" FOM "<<fom;
    }

    if(tjs.ShowerTag[12] == -5) {
      // special output for creating an ntuple
      unsigned short nTruHits;
      unsigned short mcPtclIndex = GetMCPartListIndex(tjs, ss, nTruHits);
      if(mcPtclIndex == 0 && sepPull < 4 && deltaPull < 10 && dangPull < 10) {
        // Print variables to create an ntuple
        float trueEnergy = tjs.MCPartList[mcPtclIndex]->E();
        mf::LogVerbatim myprt("TC");
        myprt<<"NTPL "<<ss.CTP<<" "<<std::fixed<<std::setprecision(2)<<trueEnergy;
        TrajPoint truTP;
        truTP.CTP = ss.CTP;
        MakeTruTrajPoint(tjs, mcPtclIndex, truTP);
        myprt<<" "<<truTP.Ang<<" "<<truTP.Delta;
//        myprt<<" "<<tj.ID;
        // number of times this DefineShowerTj was called for this shower
        Trajectory& stj = tjs.allTraj[istj];
        myprt<<" "<<stj.Pass;
        // number of points (hits) in the shower
        myprt<<" "<<ss.Pts.size();
        // shower charge
        myprt<<" "<<(int)stj.AveChg;
        myprt<<" "<<tp1Sep<<" "<<delta<<" "<<std::setprecision(3)<<dang<<" "<<(int)mom;
        myprt<<" "<<std::setprecision(2)<<sqrt(tp0Sep2)<<" "<<TrajLength(tj)<<" "<<fom;
        if(tj.ID == ss.TruParentID) {
          myprt<<" 1";
        } else {
          myprt<<" 0";
        }
      }
    } // tjs.ShowerTag[12] == -5

    return fom;
  } // ParentFOM
  
  ////////////////////////////////////////////////
  bool WrongSplitTj(TjStuff& tjs, Trajectory& tj, const unsigned short& tjEnd, ShowerStruct& ss, bool prt)
  {
    // Returns true if the trajectory was split by a 3D vertex match and the end of this trajectory is further
    // away from the shower than the partner trajectory
    // Here is a cartoon showing what we are trying to prevent. The shower is represented by a box. The trajectory
    // that is shown as (---*---) was originally reconstructed as a single trajectory. It was later split at the * position
    // by matching in 3D into two trajectories with ID = 1 and 2. We don't want to consider Tj 1 using end 0 as a parent for
    // the shower. Tj is more likely to be the real parent
    //
    //  1111111111 2222222  TjID
    //  0        1 0     1  Tj end
    //               --------------
    //               |            |
    //  ----------*-------        |
    //               |            |
    //               --------------
    if(!tj.AlgMod[kComp3DVx]) return false;
    if(tjEnd > 1) return false;
    
    // See if the other end is the end that was split. It should have a vertex with Topo = 8 or 11
    unsigned short otherEnd = 1 - tjEnd;
//    if(prt) mf::LogVerbatim("TC")<<"WSTj: otherEnd "<<otherEnd<<" vtxID "<<tj.VtxID[otherEnd];
    if(tj.VtxID[otherEnd] == 0) return false;
    unsigned short ivx = tj.VtxID[otherEnd] - 1;
    // A vertex exists but not a 3D split vertex
    if(tjs.vtx[ivx].Topo != 8 && tjs.vtx[ivx].Topo != 10) return false;
    return true;
    
  } // WrongSplitTj

  ////////////////////////////////////////////////
  void MergeNearby2DShowers(TjStuff& tjs, const CTP_t& inCTP, bool prt)
  {
    // merges showers if they share nearby Tjs
    for(unsigned short ci1 = 0; ci1 < tjs.cots.size() - 1; ++ci1) {
      ShowerStruct& ss1 = tjs.cots[ci1];
      if(ss1.CTP != inCTP) continue;
      if(ss1.TjIDs.empty()) continue;
      for(unsigned short ci2 = ci1 + 1; ci2 < tjs.cots.size(); ++ci2) {
        ShowerStruct& ss2 = tjs.cots[ci2];
        if(ss2.CTP != inCTP) continue;
        if(ss2.TjIDs.empty()) continue;
        std::vector<int> shared;
        std::set_intersection(ss1.NearTjIDs.begin(), ss1.NearTjIDs.end(), 
                              ss2.NearTjIDs.begin(), ss2.NearTjIDs.end(), std::back_inserter(shared));
        if(shared.empty()) continue;
        // add the shared Tjs to ss1
        for(auto& tjID : shared) AddTj(tjs, tjID, ci1, false, prt);
        MergeShowersAndStore(tjs, ci1, ci2, prt);
        if(prt) mf::LogVerbatim("TC")<<"MN2DS: Merged ss2 "<<ci2<<" into "<<ci1;
      } // ci2
    } // ci1
  } //MergeNearby2DShowers

  ////////////////////////////////////////////////
  void MergeOverlap(TjStuff& tjs, const CTP_t& inCTP, bool prt)
  {
    // Merge showers whose envelopes overlap each other
    
    /*
     # 0 Mode (<= 0 OFF, 1 = find showers before 3D match, 2 = find showers after 3D match)
     # 1 Max Tj MCSMom for a shower tag
     # 2 Max separation
     # 3 Min energy (MeV)
     # 4 rms width factor
     # 5 Min shower 1/2 width (WSE units)
     # 6 Min total Tj Pts
     # 7 Min Tjs
     # 8 max parent FOM
     # 9 max direction FOM
     # 10 max aspect ratio
     # 11 Debug in CTP (>10 debug cotIndex + 10)
     */
    
    if(tjs.ShowerTag[2] <= 0) return;
    if(!tjs.UseAlg[kMergeOverlap]) return;
    
    // Require that the maximum separation is about two radiation lengths
    if(prt) mf::LogVerbatim("TC")<<"MO: checking using separation cut "<<tjs.ShowerTag[2];
    
    float sepCut2 = tjs.ShowerTag[2] * tjs.ShowerTag[2];
    
    bool didMerge = true;
    while(didMerge) {
      didMerge = false;
      // See if the envelopes overlap
      for(unsigned short ict = 0; ict < tjs.cots.size() - 1; ++ict) {
        ShowerStruct& iss = tjs.cots[ict];
        if(iss.TjIDs.empty()) continue;
        if(iss.CTP != inCTP) continue;
        for(unsigned short jct = ict + 1; jct < tjs.cots.size(); ++jct) {
          ShowerStruct& jss = tjs.cots[jct];
          if(jss.TjIDs.empty()) continue;
          if(jss.CTP != iss.CTP) continue;
          bool doMerge = false;
          for(auto& ivx : iss.Envelope) {
            doMerge = PointInsideEnvelope(ivx, jss.Envelope);
            if(doMerge) break;
          } // ivx
          if(!doMerge) {
            for(auto& jvx : jss.Envelope) {
              doMerge = PointInsideEnvelope(jvx, iss.Envelope);
              if(doMerge) break;
            } // ivx
          }
          if(prt) mf::LogVerbatim("TC")<<" Envelopes "<<ict<<" "<<jct<<" overlap? "<<doMerge;
          if(!doMerge) {
            // check proximity between the envelopes
            for(auto& ivx : iss.Envelope) {
              for(auto& jvx : jss.Envelope) {
                if(PosSep2(ivx, jvx) < sepCut2) {
                  if(prt) mf::LogVerbatim("TC")<<" Envelopes "<<ict<<" "<<jct<<" are close "<<PosSep(ivx, jvx)<<" cut "<<tjs.ShowerTag[2];
                  doMerge = true;
                  break;
                }
              } // jvx
              if(doMerge) break;
            } // ivx
          } // !domerge
          if(!doMerge) continue;
          if(prt) mf::LogVerbatim("TC")<<" Merge them. Re-find shower center, etc. \n";
          // Move all of the Tjs from jct to ict
          iss.TjIDs.insert(iss.TjIDs.end(), jss.TjIDs.begin(), jss.TjIDs.end());
          iss.NearTjIDs.insert(iss.NearTjIDs.end(), jss.NearTjIDs.begin(), jss.NearTjIDs.end());
          iss.MatchedTjIDs.insert(iss.MatchedTjIDs.end(), jss.MatchedTjIDs.begin(), jss.MatchedTjIDs.end());
          std::sort(iss.MatchedTjIDs.begin(), iss.MatchedTjIDs.end());
          // drop the parent ID. It should be re-found later if it is the correct one
          iss.ParentID = 0;
          iss.NeedsUpdate = true;
          DefineShower(tjs, ict, prt);
          // kill the shower Tj
          Trajectory& jtj = tjs.allTraj[jss.ShowerTjID - 1];
          jtj.AlgMod[kKilled] = true;
          // erase jct
          tjs.cots.erase(tjs.cots.begin() + jct);
          FindExternalParent(tjs, ict, prt);
          if(prt) {
            mf::LogVerbatim("TC")<<" ShowerTj after merge";
            Trajectory& itj = tjs.allTraj[iss.ShowerTjID - 1];
            PrintTrajectory("jToi", tjs, itj, USHRT_MAX);
          }
          didMerge = true;
        } // jct
        if(didMerge) break;
      } // ict
    } // didMerge
    
  } // MergeOverlap
  
  ////////////////////////////////////////////////
  void MergeSubShowers(TjStuff& tjs, const CTP_t& inCTP, bool prt)
  {
    // Merge small showers that are downstream of larger showers
    
    if(!tjs.UseAlg[kMergeSubShowers]) return;
    
    // Require that the maximum separation is about two radiation lengths
    if(prt) mf::LogVerbatim("TC")<<"MSS: checking using radiation length cut ";
    
    constexpr float radLen = 14 / 0.3;
    
    bool didMerge = true;
    while(didMerge) {
      didMerge = false;
      for(unsigned short ict = 0; ict < tjs.cots.size(); ++ict) {
        ShowerStruct& iss = tjs.cots[ict];
        if(iss.TjIDs.empty()) continue;
        if(iss.CTP != inCTP) continue;
        TrajPoint& istp0 = tjs.allTraj[iss.ShowerTjID - 1].Pts[0];
        TrajPoint& istp2 = tjs.allTraj[iss.ShowerTjID - 1].Pts[2];
        for(unsigned short jct = 0; jct < tjs.cots.size(); ++jct) {
          if(jct == ict) continue;
          ShowerStruct& jss = tjs.cots[jct];
          if(jss.TjIDs.empty()) continue;
          if(jss.CTP != iss.CTP) continue;
          // require that the j shower be lower energy than the i shower
          if(jss.Energy > iss.Energy) continue;
          // require that it be downstream of the i shower
          TrajPoint& jstp0 = tjs.allTraj[jss.ShowerTjID - 1].Pts[0];
          float sepj0i2 = PosSep2(jstp0.Pos, istp2.Pos);
          if(sepj0i2 > PosSep2(jstp0.Pos, istp0.Pos)) continue;
          sepj0i2 = sqrt(sepj0i2);
          float trad = sepj0i2 / radLen;
          // impact parameter between the projection of istj and the jstj charge center
          float delta = PointTrajDOCA(tjs, jstp0.Pos[0], jstp0.Pos[1], istp2);
          // See if delta is consistent with the cone angle of the i shower
          float dang = delta / sepj0i2;
          if(prt) mf::LogVerbatim("TC")<<" Candidate "<<ict<<" "<<jct<<" separation "<<sepj0i2<<" radiation lengths "<<trad<<" delta "<<delta<<" dang "<<dang;
          if(trad > 3) continue;
          // TODO This needs more work
          // Require that the j energy be much lower
          if(jss.Energy > 0.3 * iss.Energy) continue;
          // There must be a correlation between dang and the energy of the j shower...
          if(dang > 0.3) continue;
          if(prt) mf::LogVerbatim("TC")<<" Merge them. Re-find shower center, etc. \n";
          // Move all of the Tjs from jct to ict
          iss.TjIDs.insert(iss.TjIDs.end(), jss.TjIDs.begin(), jss.TjIDs.end());
          iss.NearTjIDs.insert(iss.NearTjIDs.end(), jss.NearTjIDs.begin(), jss.NearTjIDs.end());
          iss.MatchedTjIDs.insert(iss.MatchedTjIDs.end(), jss.MatchedTjIDs.begin(), jss.MatchedTjIDs.end());
          std::sort(iss.MatchedTjIDs.begin(), iss.MatchedTjIDs.end());
          iss.ParentID = 0;
          iss.NeedsUpdate = true;
          DefineShower(tjs, ict, prt);
          // kill the shower Tj
          Trajectory& jtj = tjs.allTraj[jss.ShowerTjID - 1];
          jtj.AlgMod[kKilled] = true;
          // erase jct
          tjs.cots.erase(tjs.cots.begin() + jct);
          FindExternalParent(tjs, ict, prt);
          if(prt) {
            mf::LogVerbatim("TC")<<" ShowerTj after merge";
            Trajectory& itj = tjs.allTraj[iss.ShowerTjID - 1];
            PrintTrajectory("jToi", tjs, itj, USHRT_MAX);
          }
          didMerge = true;
        } // jct
        if(didMerge) break;
      } // ict
    } // didMerge
    
  } // MergeSubShowers
  
  ////////////////////////////////////////////////
  bool MergeShowersAndStore(TjStuff& tjs, unsigned short icotIndex, unsigned short jcotIndex, bool prt)
  {
    // Merge showers using shower indices. The icotIndex shower is modified in-plance.
    // The jcotIndex shower is declared obsolete. This function also re-defines the shower and
    // sets the Parent ID to 0.
    
    if(icotIndex == USHRT_MAX) return false;
    ShowerStruct& iss = tjs.cots[icotIndex];
    if(iss.TjIDs.empty()) return false;
    if(jcotIndex == USHRT_MAX) return false;
    ShowerStruct& jss = tjs.cots[jcotIndex];
    if(jss.TjIDs.empty()) return false;
    
    Trajectory& itj = tjs.allTraj[iss.ShowerTjID - 1];
    Trajectory& jtj = tjs.allTraj[jss.ShowerTjID - 1];
    
    iss.TjIDs.insert(iss.TjIDs.end(), jss.TjIDs.begin(), jss.TjIDs.end());
    // make a new trajectory using itj as a template
    Trajectory ktj = itj;
    ktj.ID = tjs.allTraj.size() + 1;
    // re-assign the hits from itj to ktj
    for(auto& kpt : ktj.Pts) std::replace(kpt.Hits.begin(), kpt.Hits.end(), itj.ID, ktj.ID);
    // transfer the jtj hits to ktj
    ktj.Pts[0].Hits.insert(ktj.Pts[0].Hits.end(), jtj.Pts[0].Hits.begin(), jtj.Pts[0].Hits.end());
    ktj.Pts[1].Hits.insert(ktj.Pts[1].Hits.end(), jtj.Pts[1].Hits.begin(), jtj.Pts[1].Hits.end());
    ktj.Pts[2].Hits.insert(ktj.Pts[2].Hits.end(), jtj.Pts[2].Hits.begin(), jtj.Pts[2].Hits.end());
    // re-assign the hits from jtj to ktj
    for(auto& kpt : ktj.Pts) {
      std::replace(kpt.Hits.begin(), kpt.Hits.end(), jtj.ID, ktj.ID);
      // Fix InTraj
      for(auto& kht : kpt.Hits) {
        if(tjs.fHits[kht].InTraj == itj.ID || tjs.fHits[kht].InTraj == jtj.ID) tjs.fHits[kht].InTraj = ktj.ID;
      }
    } //  kpt
    tjs.allTraj.push_back(ktj);
    // kill jtj
    MakeTrajectoryObsolete(tjs, iss.ShowerTjID - 1);
    MakeTrajectoryObsolete(tjs, jss.ShowerTjID - 1);
    if(prt) mf::LogVerbatim("TC")<<" killed ShowerTjs "<<iss.ShowerTjID<<" and "<<jss.ShowerTjID<<" new Tj "<<ktj.ID;
    // revise the shower
    iss.ShowerTjID = ktj.ID;
    // transfer the list of Tj IDs
    iss.TjIDs.insert(iss.TjIDs.end(), jss.TjIDs.begin(), jss.TjIDs.begin());
    std::sort(iss.TjIDs.begin(), iss.TjIDs.end());
    // clear the list of nearby Tjs
    iss.NearTjIDs.clear();
    // append the list of matched Tjs
    iss.MatchedTjIDs.insert(iss.MatchedTjIDs.end(), jss.MatchedTjIDs.begin(), jss.MatchedTjIDs.begin());
    std::sort(iss.MatchedTjIDs.begin(), iss.MatchedTjIDs.end());
    iss.ParentID = 0;
    iss.NeedsUpdate = true;
    jss.TjIDs.clear();
    DefineShower(tjs, icotIndex, prt);
    return true;

  } // MergeShowersAndStore
  
  ////////////////////////////////////////////////
  bool MergeShowerTjsAndStore(TjStuff& tjs, unsigned short istj, unsigned short jstj, bool prt)
  {
    // Merge showers using showerTj indices
    // This function is called from MergeAndStore whose function is to merge two line-like
    // trajectories and store them. This function was called because at least one of the
    // trajectories is a shower Tj. Assume that the decision to merge them has been made elsewhere.
    
    if(istj > tjs.allTraj.size() - 1) return false;
    if(jstj > tjs.allTraj.size() - 1) return false;
    
    Trajectory& itj = tjs.allTraj[istj];
    Trajectory& jtj = tjs.allTraj[jstj];
    
    if(prt) mf::LogVerbatim("TC")<<"MSTjAS: MergeShowerTjsAndStore Tj IDs "<<itj.ID<<"  "<<jtj.ID;
    
    // First we check to make sure that both are shower Tjs.
    if(!itj.AlgMod[kShowerTj] && !jtj.AlgMod[kShowerTj]) {
      if(prt) mf::LogVerbatim("TC")<<" One of these isn't a shower Tj";
      return false;
    }
    
    // We need to keep the convention used in MergeAndStore to create a new merged trajectory
    // and kill the two fragments. This doesn't require making a new shower however. We can just
    // re-purpose one of the existing showers
    unsigned short icotIndex = GetCotsIndex(tjs, itj.ID);
    if(icotIndex == USHRT_MAX) return false;
    ShowerStruct& iss = tjs.cots[icotIndex];
    if(iss.TjIDs.empty()) return false;
    unsigned short jcotIndex = GetCotsIndex(tjs, jtj.ID);
    if(jcotIndex == USHRT_MAX) return false;
    ShowerStruct& jss = tjs.cots[jcotIndex];
    if(jss.TjIDs.empty()) return false;
    
    if(!MergeShowersAndStore(tjs, icotIndex, jcotIndex, prt)) return false;
    // merge was successful. re-find the external parent and start charge
    FindExternalParent(tjs, icotIndex, prt);
    return true;
    
  } // MergeShowerTjsAndStore

  ////////////////////////////////////////////////
  bool FindChargeCenter(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Finds the charge center using all sub-structure trajectories in the cot. All of the shower
    // charge is assigned to the second TP and the charge weighted position is put in stp1.HitPos
    // and stp1.Pos
    // The charge will later be distributed between TP0 - TP2.
    // The total charge is stored in  shower Tj AveChg.
    
    if(cotIndex > tjs.cots.size() - 1) return false;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return false;
    
    unsigned short stjIndex = ss.ShowerTjID - 1;
    if(stjIndex > tjs.allTraj.size() - 1) return false;
    if(tjs.allTraj[stjIndex].Pts.size() != 3) return false;
    
    // initialize all of the points, except the first one if there is an external parent
    for(unsigned short ii = 0; ii < 3; ++ii) {
      TrajPoint& tp = tjs.allTraj[stjIndex].Pts[ii];
      tp.Chg = 0;
      tp.HitPos[0] = 0;
      tp.HitPos[1] = 0;
    }
    
    TrajPoint& stp1 = tjs.allTraj[stjIndex].Pts[1];
    
    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      if(ss.Pts[ii].Chg <= 0) {
        std::cout<<"FCC: Found point with no charge. This shouldn't happen\n";
        exit(1);
      }
      stp1.Chg += ss.Pts[ii].Chg;
      stp1.HitPos[0] += ss.Pts[ii].Chg * ss.Pts[ii].Pos[0];
      stp1.HitPos[1] += ss.Pts[ii].Chg * ss.Pts[ii].Pos[1];
    } // ii
    
    stp1.HitPos[0] /= stp1.Chg;
    stp1.HitPos[1] /= stp1.Chg;
    // Define the position only if it hasn't been done already
    if(ss.Angle == 0) stp1.Pos = stp1.HitPos;
    // Use the trajectory AveChg variable to store the total charge
    // if it isn't identified as an InShower Tj
    tjs.allTraj[stjIndex].AveChg = stp1.Chg;
    ss.Energy = ShowerEnergy(tjs, ss);
    if(prt) mf::LogVerbatim("TC")<<"FCC: cotIndex "<<cotIndex<<" HitPos "<<(int)stp1.HitPos[0]<<":"<<(int)stp1.HitPos[1]/tjs.UnitsPerTick<<" stp1.Chg "<<(int)stp1.Chg<<" Energy "<<(int)ss.Energy<<" MeV";
    return true;
  } // FindChargeCenter

  ////////////////////////////////////////////////
  void FindAngle(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Find the angle of the shower using the position of all of the TPs
    
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    
    if(ss.ParentID > 0) {
      if(prt) mf::LogVerbatim("TC")<<"FA: cotIndex "<<cotIndex<<" Using Parent Tj "<<ss.ParentID<<" angle "<<std::fixed<<std::setprecision(2)<<ss.Angle;
      return;
    }
    
    unsigned short stjIndex = ss.ShowerTjID - 1;
    if(stjIndex > tjs.allTraj.size() - 1) return;
    if(tjs.allTraj[stjIndex].Pts.size() != 3) return;

    TrajPoint& stp1 = tjs.allTraj[stjIndex].Pts[1];
    
    // Do a least squares fit using all the points
    double sum = 0.;
    double sumx = 0.;
    double sumy = 0.;
    double sumxy = 0.;
    double sumx2 = 0.;
    double sumy2 = 0.;
    double xx, yy, xr, yr, wt;

    // rotate into a coordinate system along the current shower axis
    // TODO: Use and replace RotPos?
    double cs = cos(-ss.Angle);
    double sn = sin(-ss.Angle);
    
    unsigned short nptsFit = 0;
    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      // Weight by charge
      wt = ss.Pts[ii].Chg;
      sum  += wt;
      xx = wt * (ss.Pts[ii].Pos[0] - stp1.Pos[0]);
      yy = wt * (ss.Pts[ii].Pos[1] - stp1.Pos[1]);
      xr = cs * xx - sn * yy;
      yr = sn * xx + cs * yy;
      sumx += wt * xr;
      sumy += wt * yr;
      sumx2 += wt * xr * xr;
      sumy2 += wt * yr * yr;
      sumxy += wt * xr * yr;
      ++nptsFit;
    } // ii
    
    if(nptsFit < 3) {
      if(prt) mf::LogVerbatim("TC")<<"FA: Not enough points to fit";
      return;
    }
    // calculate coefficients and std dev
    double delta = sum * sumx2 - sumx * sumx;
    if(delta == 0) {
      ss.Angle = 0;
      ss.AngleErr = 1.5;
      return;
    }
    // A is the intercept (This should be ~0 if the charge center is known)
    double A = (sumx2 * sumy - sumx * sumxy) / delta;
    // B is the slope
    double B = (sumxy * sum  - sumx * sumy) / delta;
    float dang = atan(B);
    ss.Angle += dang;
/* TODO: This gives errors that are much too small
    double ndof = nptsFit - 1;
    double varnce = (sumy2 + A*A*sum + B*B*sumx2 - 2 * (A*sumy + B*sumxy - A*B*sumx)) / ndof;
    ss.AngleErr = sqrt(varnce * sum / delta);
*/    
    // Fake a correction to the angle error instead
    ss.AngleErr = ss.AspectRatio * ss.AspectRatio * 1.57;
    
    if(prt) mf::LogVerbatim("TC")<<"FA: "<<cotIndex<<" Pos "<<ss.CTP<<":"<<PrintPos(tjs, stp1)<<" Intercept "<<(int)A<<" dang "<<dang<<" Angle "<<ss.Angle<<" Err "<<ss.AngleErr<<" npts fit "<<nptsFit;
    
  } // FindAngle
  
  ////////////////////////////////////////////////
  void FillRotPos(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Fills the RotPos vector and sorts the points along the shower axis. Note that the rotation is
    // done around stp1.Pos but the charge center is at stp1.HitPos. Pos and HitPos will be exactly the
    // same if there is no parent. The Pos position may be shifted slightly in FindExternalParent so that
    // the parent trajectory lies on the central axis of the shower. This is done so that the charge at the
    // start of the shower is calculated correctly using the parent trajectory points
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    
    Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
    // ensure that the charge center is defined
    if(stj.Pts[1].Chg == 0) return;

    // Determine the size of the shower along the axis and transverse to it. 
    // Rotate and translate each point into the coordinate system defined by tp[1]
    float cs = cos(-ss.Angle);
    float sn = sin(-ss.Angle);

    TrajPoint& stp1 = stj.Pts[1];

    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      ss.Pts[ii].RotPos[0] = ss.Pts[ii].Pos[0] - stp1.Pos[0];
      ss.Pts[ii].RotPos[1] = ss.Pts[ii].Pos[1] - stp1.Pos[1];
      // Rotate into the stp1 direction
      float along = cs * ss.Pts[ii].RotPos[0] - sn * ss.Pts[ii].RotPos[1];
      float trans = sn * ss.Pts[ii].RotPos[0] + cs * ss.Pts[ii].RotPos[1];
      ss.Pts[ii].RotPos[0] = along;
      ss.Pts[ii].RotPos[1] = trans;
    } // ii
    
    std::vector<SortEntry> sortVec(ss.Pts.size());
    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      sortVec[ii].index = ii;
      sortVec[ii].length = ss.Pts[ii].RotPos[0];
    }
    std::sort(sortVec.begin(), sortVec.end(), lessThan);
    
    // put the points vector into the sorted order
    auto tPts = ss.Pts;
    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      unsigned short indx = sortVec[ii].index;
      ss.Pts[ii] = tPts[indx];
    } // ii

    // Calculate the the aspect ratio
    float alongSum = 0;
    float transSum = 0;
    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      alongSum += std::abs(ss.Pts[ii].RotPos[0]);
      transSum += std::abs(ss.Pts[ii].RotPos[1]);
    } // ii
    
    if(alongSum > 0) {
      ss.AspectRatio = transSum / alongSum;
    } else {
      ss.AspectRatio = 100;
    }
    // Fake a correction to the angle error
    ss.AngleErr = ss.AspectRatio * ss.AspectRatio * 1.57;
    
    if(prt) mf::LogVerbatim("TC")<<"FRP: cotIndex "<<cotIndex<<" Rotation origin "<<PrintPos(tjs, stp1.Pos)<<" Angle "<<std::setprecision(2)<<ss.Angle<<" AspectRatio "<<ss.AspectRatio<<" AngleErr "<<ss.AngleErr;
    
  } // FillRotPos
  
  ////////////////////////////////////////////////
  bool DefineShowerTj(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Defines the Shower Tj, calculates the shower aspect ratio, etc. This function
    // doesn't change the state of Parent
    
    if(cotIndex > tjs.cots.size() - 1) return false;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return false;

    Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
    if(stj.Pts.size() != 3) return false;
    // ensure that the charge center is defined
    if(stj.Pts[1].Chg == 0) return false;
    
    bool hasParent = (ss.ParentID > 0);
    if(hasParent && !ss.NeedsUpdate) {
      if(prt) mf::LogVerbatim("TC")<<"DSTj: cotIndex "<<cotIndex<<" Use existing Parent Tj "<<ss.ParentID<<" info.";
      return true;
    }
    // Analyse RotPos to determine the shower direction
    float minAlong = ss.Pts[0].RotPos[0];
    float maxAlong = ss.Pts[ss.Pts.size()-1].RotPos[0];
    float sectionLength = (maxAlong - minAlong) / 3;
    float sec0 = minAlong + sectionLength;
    float sec2 = maxAlong - sectionLength;
    float chgNeg = 0;
    float transRMSNeg = 0;
    float chgPos = 0;
    float transRMSPos = 0;
    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      if(ss.Pts[ii].RotPos[0] < sec0) {
        chgNeg += ss.Pts[ii].Chg;
        transRMSNeg += ss.Pts[ii].Chg * std::abs(ss.Pts[ii].RotPos[1]);
      } else if(ss.Pts[ii].RotPos[0] > sec2) {
        chgPos += ss.Pts[ii].Chg;
        transRMSPos += ss.Pts[ii].Chg * std::abs(ss.Pts[ii].RotPos[1]);
      }
    } // ii
    if(chgNeg == 0 || chgPos == 0) return false;
    transRMSNeg /= chgNeg;
    transRMSPos /= chgPos;
    
    ss.DirectionFOM = transRMSNeg / transRMSPos;
    bool startsNeg = (transRMSNeg < transRMSPos);

    if(prt) mf::LogVerbatim("TC")<<"DSTj: "<<cotIndex<<" transRMSNeg "<<std::fixed<<std::setprecision(2)<<transRMSNeg<<" transRMSPos "<<transRMSPos<<" startsNeg? "<<startsNeg<<" DirectionFOM "<<ss.DirectionFOM;

    // reverse the points vector so that the narrow end of the shower is near Pts.begin()
    if(!hasParent && !startsNeg) {
      std::reverse(ss.Pts.begin(), ss.Pts.end());
      // change the sign of RotPos
      for(auto& sspt : ss.Pts) {
        sspt.RotPos[0] = -sspt.RotPos[0];
        sspt.RotPos[1] = -sspt.RotPos[1];
      }
      std::swap(transRMSNeg, transRMSPos);
      // flip the shower angle
      if(ss.Angle > 0) {
        ss.Angle -= M_PI;
      } else {
        ss.Angle += M_PI;
      }
      ss.DirectionFOM = 1 / ss.DirectionFOM;
      if(prt) mf::LogVerbatim("TC")<<" Reversed everything. Shower angle = "<<ss.Angle;
    } // reverse everything
    
    // Find the shower start by looking for the first point that has a small transverse distance from the shower spine. Grab
    // the longitudinal position of that point
    minAlong = 0;
    for(unsigned short ii = 0; ii < ss.Pts.size(); ++ii) {
      if(std::abs(ss.Pts[ii].RotPos[1]) < transRMSNeg) {
        minAlong = ss.Pts[ii].RotPos[0];
        break;
      }
    } // sspt
    if(minAlong == 0) return false;
    
    // define the angle of all the shower Tps
    for(auto& stp : stj.Pts) {
      stp.Ang = ss.Angle;
      stp.AngErr = ss.AngleErr;
      stp.Dir[0] = cos(stp.Ang);
      stp.Dir[1] = sin(stp.Ang);
      stp.Chg = 0;
      stp.DeltaRMS = 0;
      stp.NTPsFit = 0;
    } // stp
    
    TrajPoint& stp0 = stj.Pts[0];
    TrajPoint& stp1 = stj.Pts[1];

    // Put first shower Tj point on the shower axis using the minAlong position
    stp0.Pos[0] = minAlong * stp1.Dir[0] + stp1.Pos[0];
    stp0.Pos[1] = minAlong * stp1.Dir[1] + stp1.Pos[1];
    
    // Put the third shower Tj point on the shower axis at the maximum along position
    maxAlong = ss.Pts[ss.Pts.size()-1].RotPos[0];
    TrajPoint& stp2 = stj.Pts[2];
    stp2.Pos[0] = maxAlong * stp1.Dir[0] + stp1.Pos[0];
    stp2.Pos[1] = maxAlong * stp1.Dir[1] + stp1.Pos[1];
    
    // divide the longitudinal distance into 3 sections. Assign shower variables in these
    // sections to the 3 TPs
    sectionLength = (maxAlong - minAlong) / 3;
    sec0 = minAlong + sectionLength;
    sec2 = maxAlong - sectionLength;
    if(prt) mf::LogVerbatim("TC")<<" minAlong "<<(int)minAlong<<" maxAlong "<<(int)maxAlong<<" Section boundaries "<<(int)sec0<<" "<<(int)sec2<<" stp0 Pos "<<PrintPos(tjs, stp0.Pos)<<" stp2 Pos "<<PrintPos(tjs, stp2.Pos);
    
    // Calculate the charge and shower width in each section
    for(auto& sspt : ss.Pts) {
      unsigned short ipt = 1;
      if(sspt.RotPos[0] < sec0) ipt = 0;
      if(sspt.RotPos[0] > sec2) ipt = 2;
      stj.Pts[ipt].Chg += sspt.Chg;
      stj.Pts[ipt].DeltaRMS += sspt.Chg * sspt.RotPos[1] * sspt.RotPos[1];
      ++stj.Pts[ipt].NTPsFit;
    } // RotPt
    
    for(unsigned short ipt = 0; ipt < 3; ++ipt) {
      TrajPoint& spt = stj.Pts[ipt];
      if(spt.NTPsFit < 2) continue;
      spt.DeltaRMS = sqrt(spt.DeltaRMS / spt.Chg);
      spt.AveChg = spt.Chg;
    } // ipt
    if(stj.Pts[1].DeltaRMS == 0) stj.Pts[1].DeltaRMS = 0.5 * (stj.Pts[0].DeltaRMS + stj.Pts[2].DeltaRMS);
    
    // use Tj Pass to count the number of times this function is used
    ++stj.Pass;
    
//    if(prt) PrintTrajectory("DSTj", tjs, stj, USHRT_MAX);
    
    return true;

  } // DefineShowerTj
  
  ////////////////////////////////////////////////
  void RefineShowerTj(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Checks the properties of Shower Tj and revises them if necessary. Returns true if the
    // shower needs to be updated
    
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    
    Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
    if(stj.Pts.size() != 3) return;
    // ensure that the charge center is defined
    if(stj.Pts[1].Chg == 0) return;
    
    // Ignore fat showers
    if(ss.AspectRatio > tjs.ShowerTag[10] || ss.DirectionFOM > tjs.ShowerTag[9]) {
      if(prt) mf::LogVerbatim("TC")<<"RSTj: Not possible due to poor AspectRatio "<<ss.AspectRatio<<" or ss.DirectionFOM "<<ss.DirectionFOM;
      return;
    }

    if(ss.ParentID > 0) {
      if(prt) mf::LogVerbatim("TC")<<"RSTj: Use existing Parent Tj "<<ss.ParentID<<" info.";
      return;
    }
    
    // check the beginning of the shower to see if the points are close to the shower axis
    float sum = 0;
    float sum2 = 0;
    float chg = 0;
    unsigned short cnt = 0;
    // check the first 1/3 of the way along the shower
    unsigned short lastPt = ss.Pts.size() - 1;
    float maxAlong = ss.Pts[0].RotPos[0] + 0.3 * (ss.Pts[lastPt].RotPos[0] - ss.Pts[0].RotPos[0]);
    unsigned short firstTID = ss.Pts[0].TID;
    unsigned short cntTID = 0;
    for(auto& sspt : ss.Pts) {
      if(sspt.RotPos[0] > maxAlong) break;
      chg += sspt.Chg;
      sum += sspt.Chg * sspt.RotPos[1];
      sum2 += sspt.Chg * sspt.RotPos[1] * sspt.RotPos[1];
      if(prt && cnt < 10) mf::LogVerbatim("TC")<<"RSTj "<<sspt.Pos[0]<<":"<<(int)sspt.Pos[1]<<" RP "<<(int)sspt.RotPos[0]<<" "<<sspt.RotPos[1]<<" Chg "<<(int)sspt.Chg<<" TID "<< sspt.TID;
      ++cnt;
      if(sspt.TID == firstTID) ++cntTID;
    } // sspt
    if(chg == 0) return;
    float transAve = sum / chg;
    float arg = sum2 - chg * transAve * transAve;
    if(arg == 0) return;
    float transRMS = sqrt(arg / chg);
    transRMS /= sqrt((float)cnt);

    if(prt) mf::LogVerbatim("TC")<<"RSTj shower begin transAve "<<transAve<<" transRMS "<<transRMS;

  } // RefineShowerTj
  
  ////////////////////////////////////////////////
  void MakeShowerObsolete(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Gracefully kills the shower and the associated shower Tj
    
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    
    ss.TjIDs.clear();
    // Kill the shower Tj if it exists. This also releases the hits
    if(ss.ShowerTjID > 0) MakeTrajectoryObsolete(tjs, ss.ShowerTjID - 1);
    
    // Restore the original InShower Tjs
    // Unset the killed bit
    for(auto& tjID : ss.TjIDs) {
      Trajectory& tj = tjs.allTraj[tjID - 1];
      tj.AlgMod[kKilled] = false;
      // Restore the hit -> tj association. This is strictly only necessary if the
      // hits were re-assigned to the shower Tj but do it anyway just to be sure
      for(auto& tp : tj.Pts) {
        for(auto& iht : tp.Hits) tjs.fHits[iht].InTraj = tj.ID;
      } // tp
    } // tjID
    if(prt) mf::LogVerbatim("TC")<<"MSO: Killed ShowerTj "<<ss.ShowerTjID<<" and restored InShower Tjs.";

    
  } // MakeShowerObsolete
  
  ////////////////////////////////////////////////
  void TagShowerTjs(TjStuff& tjs, const CTP_t& inCTP, std::vector<std::vector<int>>& tjList)
  {
    // Tag Tjs with PDGCode = 11 if they have MCSMom < ShowerTag[0] and there are more than
    // ShowerTag[6] other Tjs with a separation < ShowerTag[1]. Returns a list of Tjs that meet this criteria
    
    tjList.clear();
    
    short maxMCSMom = tjs.ShowerTag[1];
    unsigned short minCnt = tjs.ShowerTag[7];
    
    for(unsigned short it1 = 0; it1 < tjs.allTraj.size(); ++it1) {
      Trajectory& tj1 = tjs.allTraj[it1];
      if(tj1.CTP != inCTP) continue;
      if(tj1.AlgMod[kKilled]) continue;
      tj1.NNeighbors = 0;
      // identified as a parent
      // ignore shower Tjs
      if(tj1.AlgMod[kShowerTj]) continue;
      // ignore stubby Tjs
      if(tj1.Pts.size() < 3) continue;
      // Cut on length and MCSMom
      if(tj1.Pts.size() > 6 && tj1.MCSMom > maxMCSMom) continue;
      if(TjHasNiceVtx(tjs, tj1, (unsigned short)tjs.ShowerTag[11])) continue;
      // TODO: Why is this being done?
      tj1.PDGCode = 0;
      std::vector<int> list;
      for(unsigned short it2 = 0; it2 < tjs.allTraj.size(); ++it2) {
        if(it1 == it2) continue;
        Trajectory& tj2 = tjs.allTraj[it2];
        if(tj2.CTP != inCTP) continue;
        if(tj2.AlgMod[kKilled]) continue;
        // identified as a parent
        // ignore shower Tjs
        if(tj2.AlgMod[kShowerTj]) continue;
        // ignore muons
        // ignore stubby Tjs
        if(tj2.Pts.size() < 3) continue;
        if(TjHasNiceVtx(tjs, tj2, (unsigned short)tjs.ShowerTag[11])) continue;
        // Cut on length and MCSMom
        if(tj2.Pts.size() > 10 && tj2.MCSMom > maxMCSMom) continue;
        unsigned short ipt1, ipt2;
	float doca = tjs.ShowerTag[2];
//        float doca = 5;
        // Find the separation between Tjs without considering dead wires
        //TrajTrajDOCA(tjs, tj1, tj2, ipt1, ipt2, doca, false);
	//XL: Find the separation between Tjs without considering dead wires
        TrajTrajDOCA(tjs, tj1, tj2, ipt1, ipt2, doca, true);
	if(doca < tjs.ShowerTag[2]) {
          // start the list with the ID of tj1
          if(list.empty()) {
            list.push_back(tj1.ID);
            AddCloseTjsToList(tjs, it1, list);
          }
          list.push_back(tj2.ID);
          AddCloseTjsToList(tjs, it2, list);
          ++tj1.NNeighbors;
        }
      } // it2
      if(list.size() > minCnt) tjList.push_back(list);
    } // it1
    
  } // TagShowerTjs
  
  ////////////////////////////////////////////////
  void FindNearbyTjs(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Find Tjs that are near the shower but are not included in it
    if(cotIndex > tjs.cots.size() - 1) return;
    ShowerStruct& ss = tjs.cots[cotIndex];
    ss.NearTjIDs.clear();
    
    std::vector<int> ntj;
    for(auto& tj : tjs.allTraj) {
      if(tj.CTP != ss.CTP) continue;
      if(tj.AlgMod[kKilled]) continue;
      // not a showerTj
      if(tj.AlgMod[kShowerTj]) continue;
      // ignore stubby Tjs
      if(tj.Pts.size() < 10) continue;
      // make sure it's not in the shower
      if(std::find(ss.TjIDs.begin(), ss.TjIDs.end(), tj.ID) != ss.TjIDs.end()) continue;
      // methodical but probably  slow
      for(auto& inTjID : ss.TjIDs) {
        unsigned short ipt1, ipt2;
        float doca = tjs.ShowerTag[2];
        Trajectory& inTj = tjs.allTraj[inTjID - 1];
        TrajTrajDOCA(tjs, tj, inTj, ipt1, ipt2, doca, false);
        if(doca < tjs.ShowerTag[2]) {
          ntj.push_back(tj.ID);
          break;
        }
      } // inTjID
    } // tj
    if(!ntj.empty()) {
      if(prt) mf::LogVerbatim("TC")<<"FNTj: cotIndex "<<cotIndex<<" found "<<ntj.size()<<" nearby Tjs";
      ss.NearTjIDs = ntj;
    }
    
  } // FindNearbyTjs
  
  ////////////////////////////////////////////////
  void AddCloseTjsToList(TjStuff& tjs, const unsigned short& itj, std::vector<int> list)
  {
    // Searches the trajectory points for hits that are used in a different trajectory and add
    // them to the list if any are found, and the MCSMomentum is not too large
    if(itj > tjs.allTraj.size() - 1) return;
    
    //short maxMom = (short)(2 * tjs.ShowerTag[1]);
    short maxMom = tjs.ShowerTag[1];
    //XL: why is maxMom is twice of the shower tag [1]? 
    for(auto& tp : tjs.allTraj[itj].Pts) {
      for(unsigned short ii = 0; ii < tp.Hits.size(); ++ii) {
        // ignore hits that are used in this trajectory
        if(tp.UseHit[ii]) continue;
        unsigned int iht = tp.Hits[ii];
        // ignore if there is no hit -> Tj association
        if(tjs.fHits[iht].InTraj <= 0) continue;
        // check the momentum
        Trajectory& tj = tjs.allTraj[tjs.fHits[iht].InTraj - 1];
        if(tj.MCSMom > maxMom) continue;
        if(TjHasNiceVtx(tjs, tj, (unsigned short)tjs.ShowerTag[11])) continue;
        // see if it is already in the list
        if(std::find(list.begin(), list.end(), tjs.fHits[iht].InTraj) != list.end()) continue;
        list.push_back(tjs.fHits[iht].InTraj);
      } // ii
    } // tp
  } // AddCloseTjsToList

  ////////////////////////////////////////////////
  void DefineEnvelope(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    
    ss.Envelope.resize(4);
    Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];    
    TrajPoint& stp0 = stj.Pts[0];
    TrajPoint& stp1 = stj.Pts[1];
    TrajPoint& stp2 = stj.Pts[2];
    
    // construct the Envelope polygon. Start with a rectangle using the fixed 1/2 width fcl input
    // expanded by the rms width at each end to create a polygon. The polygon is constructed along
    // the Pos[0] direction and then rotated into the ShowerTj direction. Use sTp1 as the origin.
    // First vertex
    ss.Envelope[0][0] = -PosSep(stp0.Pos, stp1.Pos);
    ss.Envelope[0][1] = tjs.ShowerTag[5] + tjs.ShowerTag[4] * stp0.DeltaRMS;
    // second vertex
    ss.Envelope[1][0] = PosSep(stp1.Pos, stp2.Pos);
    ss.Envelope[1][1] = tjs.ShowerTag[5] + tjs.ShowerTag[4] * stp2.DeltaRMS;
    // third and fourth are reflections of the first and second
    ss.Envelope[2][0] =  ss.Envelope[1][0];
    ss.Envelope[2][1] = -ss.Envelope[1][1];
    ss.Envelope[3][0] =  ss.Envelope[0][0];
    ss.Envelope[3][1] = -ss.Envelope[0][1];
    
    float length = ss.Envelope[1][0] - ss.Envelope[0][0];
    float width = ss.Envelope[0][1] + ss.Envelope[1][1];
    ss.EnvelopeArea = length * width;

    // Rotate into the stp1 coordinate system
    float cs = cos(stp1.Ang);
    float sn = sin(stp1.Ang);
    for(auto& vtx : ss.Envelope) {
      // Rotate along the stj shower axis
      float pos0 = cs * vtx[0] - sn * vtx[1];
      float pos1 = sn * vtx[0] + cs * vtx[1];
      // translate
      vtx[0] = pos0 + stp1.Pos[0];
      vtx[1] = pos1 + stp1.Pos[1];
    } // vtx
    // Find the charge density inside the envelope
    ss.ChgDensity = (stp0.Chg + stp1.Chg + stp2.Chg) / ss.EnvelopeArea;
    if(prt) {
      mf::LogVerbatim myprt("TC");
      myprt<<"DE: "<<cotIndex<<" Envelope";
      for(auto& vtx : ss.Envelope) myprt<<" "<<(int)vtx[0]<<":"<<(int)(vtx[1]/tjs.UnitsPerTick);
      myprt<<" Area "<<(int)ss.EnvelopeArea;
      myprt<<" ChgDensity "<<ss.ChgDensity;
    }
    // This is the last function used to update a shower
    ss.NeedsUpdate = false;
  } // DefineEnvelope  
  
  ////////////////////////////////////////////////
  void AddTjsInsideEnvelope(TjStuff& tjs, const unsigned short& cotIndex, bool prt, int mode)
  {
    // This function adds Tjs to the shower. It updates the shower parameters.
    
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.Envelope.empty()) return;
    if(ss.TjIDs.empty()) return;
    
    unsigned short nadd = 0;
    for(auto& tj : tjs.allTraj) {
      if(tj.CTP != ss.CTP) continue;
      if(tj.AlgMod[kKilled]) continue;
      if(tj.AlgMod[kInShower]) continue;
      if(tj.AlgMod[kShowerTj]) continue;

      if(mode==1)
	{if(TjHasNiceVtx(tjs, tj, (unsigned short)tjs.ShowerTag[11])) continue;}

      //if(TjHasNiceVtx(tjs, tj, (unsigned short)tjs.ShowerTag[11])) continue;

      // This shouldn't be necessary but do it for now
      if(std::find(ss.TjIDs.begin(), ss.TjIDs.end(), tj.ID) != ss.TjIDs.end()) continue;
      // See if both ends are outside the envelope
      bool end0Inside = PointInsideEnvelope(tj.Pts[tj.EndPt[0]].Pos, ss.Envelope);
      bool end1Inside = PointInsideEnvelope(tj.Pts[tj.EndPt[1]].Pos, ss.Envelope);
      if(!end0Inside && !end1Inside) continue;
      // at least one end is inside. See if both are inside
      if(end0Inside && end1Inside) {
        // Fully contained
        // TODO: See if the Tj direction is compatible with the shower?
        if(AddTj(tjs, tj.ID, cotIndex, false, prt)) ++nadd;
        ++nadd;
        continue;
      } // both ends inside
      // Require high momentum Tjs be aligned with the shower axis
      // TODO also require high momentum Tjs close to the shower axis?
      if(tj.MCSMom > 500) {
        float tjAngle = tj.Pts[tj.EndPt[0]].Ang;
        float dangPull = std::abs(tjAngle -ss.AngleErr) / ss.AngleErr;
        if(dangPull > 2) continue;
      } // high momentum
      if(AddTj(tjs, tj.ID, cotIndex, false, prt)) ++nadd;
    } // tj
    
    if(nadd > 0) {
      if(prt) mf::LogVerbatim("TC")<<"ATIE:  Added "<<nadd<<" trajectories. Calling DefineShower... ";
      DefineShower(tjs, cotIndex, prt);
      return;
    } else {
      if(prt) mf::LogVerbatim("TC")<<"ATIE:  No new trajectories added to envelope ";
      return;
    }
        
  } // AddTjsInsideEnvelope
  
  ////////////////////////////////////////////////
  bool AddLooseHits(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Add hits that are inside the envelope to the shower if they are loose, i.e. not
    // used by any trajectory. This function returns true if the set of hits is different than
    // the current set. The calling function should update the shower if this is the case.
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.Envelope.empty()) return false;
    if(ss.TjIDs.empty()) return false;

    geo::PlaneID planeID = DecodeCTP(ss.CTP);
    unsigned short ipl = planeID.Plane;
    
    Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
    TrajPoint& stp0 = stj.Pts[0];
    // start a list of new hits
    std::vector<unsigned int> newHits;
    
    // look for hits inside the envelope. Find the range of wires that spans the envelope
    float fLoWire = 1E6;
    float fHiWire = 0;
    // and the range of ticks
    float loTick = 1E6;
    float hiTick = 0;
    for(auto& vtx : ss.Envelope) {
      if(vtx[0] < fLoWire) fLoWire = vtx[0];
      if(vtx[0] > fHiWire) fHiWire = vtx[0];
      if(vtx[1] < loTick) loTick = vtx[1];
      if(vtx[1] > hiTick) hiTick = vtx[1];
    } // vtx
    loTick /= tjs.UnitsPerTick;
    hiTick /= tjs.UnitsPerTick;
    unsigned int loWire = std::nearbyint(fLoWire);
    unsigned int hiWire = std::nearbyint(fHiWire) + 1;
    if(hiWire > tjs.LastWire[ipl]-1) hiWire = tjs.LastWire[ipl]-1;
    std::array<float, 2> point;
    for(unsigned int wire = loWire; wire < hiWire; ++wire) {
      // skip bad wires or no hits on the wire
      if(tjs.WireHitRange[ipl][wire].first < 0) continue;
      unsigned int firstHit = (unsigned int)tjs.WireHitRange[ipl][wire].first;
      unsigned int lastHit = (unsigned int)tjs.WireHitRange[ipl][wire].second;
      for(unsigned int iht = firstHit; iht < lastHit; ++iht) {
        // used in a trajectory?
        if(tjs.fHits[iht].InTraj != 0) continue;
        if(tjs.IgnoreNegChiHits && tjs.fHits[iht].GoodnessOfFit < 0) continue;
        // inside the tick range?
        if(tjs.fHits[iht].PeakTime < loTick) continue;
        // Note that hits are sorted by increasing time so we can break here
        if(tjs.fHits[iht].PeakTime > hiTick) break;
        // see if this hit is inside the envelope
        point[0] = tjs.fHits[iht].WireID.Wire;
        point[1] = tjs.fHits[iht].PeakTime * tjs.UnitsPerTick;
        if(!PointInsideEnvelope(point, ss.Envelope)) continue;
        newHits.push_back(iht);
      } // iht
    } // wire
    
    // no new hits and no old hits. Nothing to do
    if(newHits.empty()) {
      if(prt) mf::LogVerbatim("TC")<<"ALH: No new loose hits found";
      return false;
    }
    
    // Update
    stp0.Hits.insert(stp0.Hits.end(), newHits.begin(), newHits.end());
    for(auto& iht: newHits) tjs.fHits[iht].InTraj = stj.ID;
    
    if(prt) mf::LogVerbatim("TC")<<"ALH: Added "<<stp0.Hits.size()<<" hits to stj "<<stj.ID;
    return true;

  } // AddLooseHits
  
  ////////////////////////////////////////////////
  void FindStartChg(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Finds the charge at the start of a shower
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    
    ss.StartChg = 0;
    ss.StartChgErr = 1;
    
    if(ss.AspectRatio > tjs.ShowerTag[10] || ss.DirectionFOM > tjs.ShowerTag[9]) {
      if(prt) mf::LogVerbatim("TC")<<"FSC: Not possible due to poor AspectRatio "<<ss.AspectRatio<<" or ss.DirectionFOM "<<ss.DirectionFOM;
      return;
    }

    // Create and fill a vector of the charge at the beginning of the shower in 1 WSE bins
    auto schg = StartChgVec(tjs, cotIndex, prt);
    if(schg.empty()) return;

    // Look for two consecutive charge entries. Use the second one
    // for the initial guess at the charge
    unsigned short startPt = USHRT_MAX;
    float chg = 0;
    for(unsigned short ii = 0; ii < schg.size() - 1; ++ii) {
      if(schg[ii] > 0 && schg[ii + 1] > 0) {
        startPt = ii + 1;
        chg = schg[ii + 1];
        break;
      }
    }
    if(startPt == USHRT_MAX) return;
    
    // get an initial average and rms using all the points
    float ave = 0;
    float rms = 0;
    float cnt = 0;
    for(unsigned short ii = startPt; ii < schg.size() - 1; ++ii) {
      ave += schg[ii];
      rms += schg[ii] * schg[ii];
      ++cnt;
    } // ii
    ave /= cnt;
    rms = rms - cnt * ave * ave;
    if(rms < 0) return;
    rms = sqrt(rms / (cnt - 1));
    
    if(prt) {
      mf::LogVerbatim myprt("TC");
      myprt<<"FSC: schg:";
      for(unsigned short ii = 0; ii < 20; ++ii) myprt<<" "<<(int)schg[ii];
      myprt<<"\n First guess at the charge "<<(int)chg<<" average charge of all bins "<<(int)ave<<" rms "<<(int)rms;
    }
    
    // initial guess at the charge rms
    rms = 0.8 * chg;
    
    // Correct for dead wires in this region - maybe later...
//    unsigned short nDeadWires = DeadWireCount();
    
    unsigned short nBinsAverage = 5;
    double maxChg = 2 * chg;
    for(unsigned short nit = 0; nit < 2; ++nit) {
      double sum = 0;
      double sum2 = 0;
      double cnt = 0;
      for(unsigned short ii = startPt; ii < schg.size() - 1; ++ii) {
        // break if we find 2 consecutive high charge points
        if(schg[ii] > maxChg && schg[ii + 1] > maxChg) break;
        // or two zeros
        if(schg[ii] == 0 && schg[ii + 1] == 0) break;
        if(schg[ii] > maxChg) continue;
        sum += schg[ii];
        sum2 += schg[ii] * schg[ii];
        ++cnt;
        if(cnt == nBinsAverage) break;
      } // ii
      // check for a failure
      if(cnt < 3) {
        if(prt) mf::LogVerbatim("TC")<<" nit "<<nit<<" cnt "<<cnt<<" is too low. sum "<<(int)sum<<" maxChg "<<(int)maxChg;
        // try to recover. Pick the next point
        ++startPt;
        chg = schg[startPt];
        maxChg = 2 * chg;
        continue;
      }
      // convert sum to the average charge
      chg = sum / cnt;
      double arg = sum2 - cnt * chg * chg;
      if(arg < 0) break;
      rms = sqrt(arg / (cnt - 1));
      // don't allow the rms to get crazy
      double maxrms = 0.5 * sum;
      if(rms > maxrms) rms = maxrms;
      maxChg = chg + 2 * rms;
      if(prt) mf::LogVerbatim("TC")<<" nit "<<nit<<" cnt "<<cnt<<" chg "<<(int)chg<<" rms "<<(int)rms<<" maxChg "<<(int)maxChg<<" nBinsAverage "<<nBinsAverage;
      nBinsAverage = 20;
    } // nit
    
    ss.StartChg = chg;
    ss.StartChgErr = rms;
    
    if(prt) mf::LogVerbatim("TC")<<"FSC: cotIndex "<<cotIndex<<" Starting charge "<<(int)ss.StartChg<<" +/- "<<(int)ss.StartChgErr<<" startPt  "<<startPt;
    
  } // FindStartChg
  
  ////////////////////////////////////////////////
  std::vector<float> StartChgVec(TjStuff& tjs, const unsigned short& cotIndex, bool prt)
  {
    // Returns a histogram vector of the charge in bins of 1 WSE unit at the start of the shower

    ShowerStruct& ss = tjs.cots[cotIndex];
    constexpr unsigned short nbins = 20;
    std::vector<float> schg(nbins);
    if(ss.TjIDs.empty()) return schg; 
    TrajPoint& stp1 = tjs.allTraj[ss.ShowerTjID-1].Pts[1];
    
    // move the min along point back by 2 WSE so that most of the charge in the hits in the
    // first point is included in the histogram
    float minAlong = ss.Pts[0].RotPos[0] - 2;
    
    float maxTrans = 4;
    // Tighten up on the maximum allowed transverse position if there is a parent
    if(ss.ParentID > 0) maxTrans = 1;
    float cs = cos(-ss.Angle);
    float sn = sin(-ss.Angle);
    std::array<float, 2> chgPos;
    float along, arg;

    for(auto& sspt : ss.Pts) {
      unsigned short indx = (unsigned short)((sspt.RotPos[0] - minAlong));
      if(indx > nbins - 1) break;
      // Count the charge if it is within a few WSE transverse from the shower axis
      if(std::abs(sspt.RotPos[1]) > maxTrans) continue;
      unsigned int iht = sspt.HitIndex;
      float& peakTime = tjs.fHits[iht].PeakTime;
      float& amp = tjs.fHits[iht].PeakAmplitude;
      float& rms = tjs.fHits[iht].RMS;
      chgPos[0] = tjs.fHits[iht].WireID.Wire - stp1.Pos[0];
      for(float time = peakTime - 2.5 * rms; time < peakTime + 2.5 * rms; ++time) {
        chgPos[1] = time * tjs.UnitsPerTick - stp1.Pos[1];
        along = cs * chgPos[0] - sn * chgPos[1];
        if(along < minAlong) continue;
        indx = (unsigned short)(along - minAlong);
        if(indx > nbins - 1) continue;
        arg = (time - peakTime) / rms;
        schg[indx] += amp * exp(-0.5 * arg * arg);
      } // time
    } // sspt

    return schg;
  } // StartChgVec
  
  ////////////////////////////////////////////////
  void DumpShowerPts(TjStuff& tjs, const unsigned short& cotIndex)
  {
    // Print the shower points to the screen. The user should probably pipe the output to a text file
    // then grep this file for the character string PTS which is piped to a text file which can then be
    // imported into Excel, etc
    // Finds the charge at the start of a shower
    if(cotIndex > tjs.cots.size() - 1) return;
    
    ShowerStruct& ss = tjs.cots[cotIndex];
    if(ss.TjIDs.empty()) return;
    std::cout<<"PTS Pos0  Pos1   RPos0 RPos1 Chg TID\n";
    for(auto& pt : ss.Pts) {
      std::cout<<"PTS "<<std::fixed<<std::setprecision(1)<<pt.Pos[0]<<" "<<pt.Pos[1]<<" "<<pt.RotPos[0]<<" "<<pt.RotPos[1];
      std::cout<<" "<<(int)pt.Chg<<" "<<pt.TID;
      std::cout<<"\n";
    }
    
  } // DumpShower
  
  ////////////////////////////////////////////////
  void CheckQuality(TjStuff& tjs, const CTP_t& inCTP, bool prt)
  {
    // drop those that don't meet the requirements
    for(unsigned short cotIndex = 0; cotIndex < tjs.cots.size(); ++cotIndex) {
      ShowerStruct& ss = tjs.cots[cotIndex];
      if(ss.CTP != inCTP) continue;
      if(ss.TjIDs.empty()) continue;
      // enough Tjs?
      unsigned short ntjs = ss.TjIDs.size();
      bool killit = (ntjs < tjs.ShowerTag[7]);
      // Kill runt showers
      if(!killit) killit = (ss.Energy < tjs.ShowerTag[3]);
      if(prt) mf::LogVerbatim("TC")<<"cotIndex "<<cotIndex<<" nTjs "<<ss.TjIDs.size()<<" nTjs "<<ss.TjIDs.size()<<" killit? "<<killit;
      if(!killit) {
        // count the number of Tj points
        unsigned short nTjPts = 0;
        for(auto& tjID : ss.TjIDs) {
          Trajectory& tj = tjs.allTraj[tjID - 1];
          nTjPts += NumPtsWithCharge(tjs, tj, false);
        }  // tjID
        if(nTjPts < tjs.ShowerTag[6]) killit = true;
        if(prt) mf::LogVerbatim("TC")<<"    "<<" nTjPts "<<nTjPts<<" killit? "<<killit;
      } // !killit
      if(killit) {
        MakeShowerObsolete(tjs, cotIndex, prt);
      } else {
        if(tjs.allTraj[ss.ShowerTjID - 1].AlgMod[kKilled]) {
          std::cout<<"FS logic error: ShowerTj "<<tjs.allTraj[ss.ShowerTjID - 1].ID<<" is killed\n";
          tjs.allTraj[ss.ShowerTjID - 1].AlgMod[kKilled] = false;
        }
        // A good shower. Set the pdgcode of InShower Tjs to 11
        for(auto& tjID : ss.TjIDs) {
          Trajectory& tj = tjs.allTraj[tjID - 1];
          tj.PDGCode = 11;
          // Clobber 2D vertices that are inside the shower
          for(unsigned short end = 0; end < 2; ++end) {
            if(tj.VtxID[end] > 0) {
              VtxStore& vx2 = tjs.vtx[tj.VtxID[end]-1];
              bool killMe = (vx2.Score < tjs.ShowerTag[11]);
              // don't kill the 2D vertex if it is attached to the far end of the parent Tj
              if(killMe && ss.ParentID == tjID) {
                unsigned short farEnd = FarEnd(tjs, tj, ss);
                if(farEnd == end) killMe = false;
              }
              if(killMe) {
                if(prt) mf::LogVerbatim("TC")<<"Clobber vtx "<<tj.VtxID[end]<<" Score "<<vx2.Score<<" Vtx3ID "<<tjs.vtx[tj.VtxID[end]-1].Vtx3ID;
                MakeVertexObsolete(tjs, tj.VtxID[end]);
              }
            }
          } // end
        }
      } // don't killit
    } // ic
    
  } // CheckQuality

  ////////////////////////////////////////////////
  bool TransferTjHits(TjStuff& tjs, const CTP_t& inCTP, bool prt)
  {
    // Transfer InShower hits to the shower Tj 
    
    bool newShowers = false;
    for(unsigned short ish = 0; ish < tjs.cots.size(); ++ish) {
      ShowerStruct& ss = tjs.cots[ish];
      // Ensure that this is the correct CTP
      if(ss.CTP != inCTP) continue;
      // Ensure that it is valid
      if(ss.TjIDs.empty()) continue;
      if(ss.ShowerTjID == 0) continue;
      // Tp 1 of stj will get all of the shower hits
      Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
      if(!stj.Pts[1].Hits.empty()) {
        std::cout<<"TTjH: ShowerTj "<<stj.ID<<" already has hits. This can't be right\n";
        continue;
      }
      stj.PDGCode = 11;
      // Note that UseHit is not used since the size is limited to 16
      for(auto& tjID : ss.TjIDs) {
        unsigned short itj = tjID - 1;
        if(tjs.allTraj[itj].VtxID[0] != 0 && tjs.allTraj[itj].VtxID[0] != stj.VtxID[0]) {
          std::cout<<"TTjH: Trying to transfer hits on Tj "<<tjID<<" attached to a vertex "<<tjs.allTraj[itj].VtxID[0]<<" at end0 that is not the showerTj vertex "<<stj.VtxID[0]<<"\n";
          continue;
        }
        if(tjs.allTraj[itj].AlgMod[kShowerTj]) {
          std::cout<<"TTjH: Coding error. Tj "<<tjID<<" is a ShowerTj but is in TjIDs\n";
          continue;
        }
        if(!tjs.allTraj[itj].AlgMod[kInShower]) {
          std::cout<<"TTjH: Coding error. Trying to transfer Tj "<<tjID<<" hits but it isn't an InShower Tj\n";
          continue;
        }
        auto thits = PutTrajHitsInVector(tjs.allTraj[itj], kUsedHits);
        stj.Pts[1].Hits.insert(stj.Pts[1].Hits.end(), thits.begin(), thits.end());
        // kill Tjs that are in showers
        tjs.allTraj[itj].AlgMod[kKilled] = true;
      } //  tjID
      // re-assign the hit -> stj association
      for(auto& iht : stj.Pts[1].Hits) tjs.fHits[iht].InTraj = stj.ID;
      newShowers = true;
    } // ish
    return newShowers;
  } // TransferTjHits

  ////////////////////////////////////////////////
  unsigned short GetCotsIndex(TjStuff& tjs, const unsigned short& ShowerTjID)
  {
    for(unsigned short ii = 0; ii < tjs.cots.size(); ++ii) {
      if(ShowerTjID == tjs.cots[ii].ShowerTjID) return ii;
    } // iii
    return USHRT_MAX;
    
  } // GetCotsIndex

  ////////////////////////////////////////////////
  float ShowerEnergy(const TjStuff& tjs, const ShowerStruct& ss)
  {
    if(ss.TjIDs.empty()) return 0;
    if(ss.ShowerTjID == 0) return 0;
    
    // Conversion from shower charge to energy in MeV. 0.0143 comes from an eye-bal fit.
    // Divide by the expected shower containment of 90%. This needs to be calculated directly
    constexpr float fShMeVPerChg = 0.0143 / 0.9;
    
    const Trajectory& stj = tjs.allTraj[ss.ShowerTjID - 1];
    return fShMeVPerChg * stj.AveChg;
    
  } // ShowerEnergy
  
  ////////////////////////////////////////////////
  unsigned short Create2DShower(TjStuff& tjs, const std::vector<int>& tjl)
  {
    // Create a shower and shower Tj using Tjs in the list
    if(tjl.empty()) return USHRT_MAX;
    
    // Get the CTP from the first Tj
    Trajectory stj;
    stj.CTP = tjs.allTraj[tjl[0]-1].CTP;
    // with three points
    stj.Pts.resize(3);
    for(auto& stp : stj.Pts) {
      stp.CTP = stj.CTP;
      // set all UseHit bits true so we don't get confused
      stp.UseHit.set();
    }
    stj.EndPt[0] = 0;
    stj.EndPt[1] = 2;
    stj.ID = tjs.allTraj.size() + 1;
    // Declare that stj is a shower Tj
    stj.AlgMod[kShowerTj] = true;
    tjs.allTraj.push_back(stj);
    // Create the shower struct
    ShowerStruct ss;
    ss.CTP = stj.CTP;
    // assign all TJ IDs to this ShowerStruct
    ss.TjIDs = tjl;
    ss.ShowerTjID = stj.ID;
    // try to define the true shower parent Tj
    unsigned short nTruHits;
    // Find the MC particle that matches with these InShower Tjs
    unsigned short mcpIndex = GetMCPartListIndex(tjs, ss, nTruHits);
    // Find the Tj that is closest to the start of this MC Particle
    if(mcpIndex != USHRT_MAX) ss.TruParentID = MCParticleStartTjID(tjs, mcpIndex, ss.CTP);
    // put it in TJ stuff. The rest of the info will be added later
    tjs.cots.push_back(ss);
    return tjs.cots.size() - 1;
    
  } // Create2DShower

  void SaveTjInfo(TjStuff& tjs,  const CTP_t& inCTP, std::vector<std::vector<int>>& tjList,
		  unsigned int stageNum) {
    
    for(unsigned short it1 = 0; it1 < tjs.allTraj.size(); ++it1) {
      Trajectory& tj1 = tjs.allTraj[it1];
      if(tj1.CTP != inCTP) continue;
      if(tj1.AlgMod[kKilled]) continue;

      SaveTjInfoStuff(tjs, inCTP, tj1,  stageNum);

      int trajID = tj1.ID;
      bool inShower = false;

      for (size_t l1 = 0; l1 < tjList.size(); ++l1) {
	if (inShower) break;
	for (size_t l2 = 0; l2 < tjList[l1].size(); ++l2) {

	  if (trajID == tjList[l1][l2]) {
	    tjs.stv.ShowerID.back() = l1;
	    inShower = true;
	    break;
	  }
	} // end list loop 2
      } // end list loop 1
    } // end tjs loop

    // add meaningless envelope to list for counting purposes
    // envelopes are defined once DefineShower is called
    // fill four times, one for each side of polygon
    for (int i = 0; i < 8; i++) {
      tjs.stv.Envelope.push_back(-999);
      tjs.stv.EnvStage.push_back(stageNum);
      tjs.stv.EnvPlane.push_back(-1);
      tjs.stv.EnvShowerID.push_back(-1);
    }

  } // SaveTjInfo (tjlist)

  void SaveTjInfo(TjStuff& tjs,  const CTP_t& inCTP, const unsigned short& cotIndex,
                  unsigned int stageNum) {

    ShowerStruct& ss = tjs.cots[cotIndex];

    for(unsigned short it1 = 0; it1 < tjs.allTraj.size(); ++it1) {

      // right now I exclude the shower trajectory
      if (it1 == tjs.allTraj.size() - 1) continue;
      
      Trajectory& tj1 = tjs.allTraj[it1];
      if(tj1.CTP != inCTP) continue;
      if(tj1.AlgMod[kKilled]) continue;

      SaveTjInfoStuff(tjs, inCTP, tj1, stageNum);

      int trajID = tj1.ID;

      //      ShowerStruct& ss = tjs.cots[cotIndex];

      for (size_t i = 0; i < ss.TjIDs.size(); ++i) {
	if (trajID == ss.TjIDs[i]) {
	  tjs.stv.ShowerID.back() = cotIndex;
	  break;
	}
	// check if tj is shower parent. if so, add to ttree
	// and mark parent flag
	if (trajID == ss.ParentID) {
	  tjs.stv.ShowerID.back() = cotIndex;
	  tjs.stv.IsShowerParent.back() = 1;
	  break;

	}
      } // ss TjID loop

    } // end tjs loop

    // add envelope information to showertreevars
    geo::PlaneID iPlnID = DecodeCTP(ss.CTP);
    for (int i = 0; i < 8; i++) {
      tjs.stv.EnvStage.push_back(stageNum);
      tjs.stv.EnvPlane.push_back(iPlnID.Plane);
      tjs.stv.EnvShowerID.push_back(cotIndex);
    }
    tjs.stv.Envelope.push_back(ss.Envelope[0][0]);
    tjs.stv.Envelope.push_back(ss.Envelope[0][1]/tjs.UnitsPerTick);
    tjs.stv.Envelope.push_back(ss.Envelope[1][0]);
    tjs.stv.Envelope.push_back(ss.Envelope[1][1]/tjs.UnitsPerTick);
    tjs.stv.Envelope.push_back(ss.Envelope[2][0]);
    tjs.stv.Envelope.push_back(ss.Envelope[2][1]/tjs.UnitsPerTick);
    tjs.stv.Envelope.push_back(ss.Envelope[3][0]);
    tjs.stv.Envelope.push_back(ss.Envelope[3][1]/tjs.UnitsPerTick);

  } // SaveTjInfo (cots)
 

  void SaveTjInfoStuff(TjStuff& tjs,  const CTP_t& inCTP, Trajectory& tj,  unsigned int stageNum) {
    //    int trajID = tj.ID;
    //    if (stageNum == 1 && tjs.stv.nPlanes) tjs.stv.nPlanes = 999;

    TrajPoint& beginPoint = tj.Pts[tj.EndPt[0]];
    TrajPoint& endPoint = tj.Pts[tj.EndPt[1]];

    tjs.stv.BeginWir.push_back(std::nearbyint(beginPoint.Pos[0]));
    tjs.stv.BeginTim.push_back(std::nearbyint(beginPoint.Pos[1]/tjs.UnitsPerTick));
    tjs.stv.BeginAng.push_back(beginPoint.Ang);
    tjs.stv.BeginChg.push_back(beginPoint.Chg);
    tjs.stv.BeginVtx.push_back(tj.VtxID[0]);

    tjs.stv.EndWir.push_back(std::nearbyint(endPoint.Pos[0]));
    tjs.stv.EndTim.push_back(std::nearbyint(endPoint.Pos[1]/tjs.UnitsPerTick));
    tjs.stv.EndAng.push_back(endPoint.Ang);
    tjs.stv.EndChg.push_back(endPoint.Chg);
    tjs.stv.EndVtx.push_back(tj.VtxID[1]);

    tjs.stv.MCSMom.push_back(tj.MCSMom);

    tjs.stv.ShowerID.push_back(-1);
    tjs.stv.IsShowerParent.push_back(-1);
    tjs.stv.StageNum.push_back(stageNum);
    tjs.stv.nStages = stageNum;
    geo::PlaneID iPlnID = DecodeCTP(tj.CTP);
    tjs.stv.PlaneNum.push_back(iPlnID.Plane);

    tjs.stv.nPlanes = tjs.NumPlanes;
  } // SaveTjInfo
}
