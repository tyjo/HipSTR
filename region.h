#ifndef REGION_H_
#define REGION_H_

#include <assert.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <stdlib.h>

#include "error.h"

class Region{
private:
  std::string chrom_, name_;
  int32_t start_, stop_;
  int period_;
public:
  Region(std::string chrom, int32_t start, int32_t stop, int period){
    assert(stop > start);
    chrom_ = chrom; start_ = start; stop_ = stop; period_ = period; name_ = "";
  }

  Region(std::string chrom, int32_t start, int32_t stop, int period, std::string name){
    assert(stop > start);
    chrom_ = chrom; start_ = start; stop_ = stop; period_ = period; name_ = name;
  }

  const std::string& chrom() const { return chrom_;  }
  const std::string& name()  const { return name_;   }
  int32_t start()            const { return start_;  }
  int32_t  stop()            const { return stop_;   }
  int     period()           const { return period_; }
  Region*   copy()           const { return new Region(chrom_, start_, stop_, period_, name_); }

  void set_start(int32_t start){ start_ = start; }
  void set_stop(int32_t stop)  { stop_  = stop;  }

  std::string str(){
    std::stringstream ss;
    ss << chrom_ << ":" << start_ << "-" << stop_;
    return ss.str();
  }

  bool operator<(const Region &r)  const {
    if (chrom_.compare(r.chrom()) != 0)
      return chrom_.compare(r.chrom()) < 0;
    if (start_ != r.start())
      return start_ < r.start();
    if (stop_ != r.stop())
      return stop_ < r.stop();
    return false;
  }

};

void readRegions(std::string& input_file, std::vector<Region>& regions, uint32_t max_regions, std::string chrom, std::ostream& logger);

void orderRegions(std::vector<Region>& regions);

void orderRegions(std::vector<Region>& input_regions, 
		  std::vector< std::vector<Region> >& output_regions, 
		  std::map<std::string, int>& chrom_order);

class RegionGroup {
  std::vector<Region> regions_;
  std::string chrom_;
  int32_t start_;
  int32_t stop_;

 public:
  RegionGroup(Region& region){
    regions_.push_back(region);
    chrom_ = region.chrom();
    start_ = region.start();
    stop_  = region.stop();
  }

  const std::vector<Region>& regions(){
    return regions_;
  }

  const std::string& chrom() const { return chrom_;          }
  int32_t  start()           const { return start_;          }
  int32_t  stop()            const { return stop_;           }
  int      num_regions()     const { return regions_.size(); }

  void add_region(Region& region){
    if (region.chrom().compare(chrom_) != 0)
      printErrorAndDie("RegionGroup can only consist of regions on a single chromosome");
    start_ = std::min(start_, region.start());
    stop_  = std::max(stop_,  region.stop());
    regions_.push_back(region);
    std::sort(regions_.begin(), regions_.end());
  }
};
#endif
