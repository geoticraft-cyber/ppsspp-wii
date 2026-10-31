// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside namespace Wii; no heap, locks, callbacks or std::function.
struct CpuBucket { uint64_t ticks, inclusive, calls, samples; };
struct CpuSite { uint32_t key; unsigned kind; uint64_t ticks, calls; };
class CpuScope;
struct CpuProfileState {
  bool enabled;
  unsigned stride, serial[3];
  CpuScope *top;
  CpuBucket buckets[unsigned(CpuPhase::Count)];
  CpuSite sites[64]; // bounded four-way map; evictions are explicitly counted
  uint64_t counters[12], collisions;
};
inline CpuProfileState &CpuProfileData() {
#if defined(PPSSPP_WII_PORTABLE_TEST)
  static thread_local CpuProfileState data={};
#else
  static CpuProfileState data={};
#endif
  return data;
}
// Clock/log functions are defined outside the namespace by CpuProfile.h.
inline void CpuProfileConfigure(bool enabled,unsigned stride=64) {
  auto &s=CpuProfileData();
  // Only configure between games, with no live scopes.
  if(s.top)return;
  s=CpuProfileState();s.enabled=enabled;
  s.stride=(stride==1||stride==8||stride==64||stride==256)?stride:64;
}
class CpuScope {
  CpuScope *parent_;
  uint64_t start_,children_;
  unsigned weight_;
  CpuPhase phase_;
  uint32_t key_;
public:
  explicit CpuScope(CpuPhase phase,bool sampled=false,uint32_t key=0) {
    auto &s=CpuProfileData();
    if(!s.enabled) {weight_=0;return;}
    phase_=phase;key_=key;children_=0;
    auto &b=s.buckets[unsigned(phase)];++b.calls;
    weight_=sampled?s.stride:1;
    if(sampled && ((s.serial[unsigned(phase)%3]++)&(weight_-1))) {weight_=0;return;}
    ++b.samples;parent_=s.top;s.top=this;start_=CpuProfileClock();
  }
  CpuScope(const CpuScope &)=delete;
  ~CpuScope() {End();}
  bool Active() const {return weight_!=0;}
  void End() {
    if(!weight_)return;
    const uint64_t elapsed=CpuProfileClock()-start_;
    auto &s=CpuProfileData();auto &b=s.buckets[unsigned(phase_)];
    const uint64_t own=elapsed>=children_?elapsed-children_:0;
    b.ticks+=own*weight_;b.inclusive+=elapsed*weight_;
    s.top=parent_;if(parent_)parent_->children_+=elapsed;
    if(key_) {
      const unsigned base=(((key_>>2)^(key_>>10)^unsigned(phase_))&15)*4;
      unsigned chosen=base;
      for(unsigned i=base;i<base+4;++i) {
        if(s.sites[i].calls&&s.sites[i].key==key_&&s.sites[i].kind==unsigned(phase_)) {chosen=i;break;}
        if(!s.sites[i].calls||s.sites[i].calls<s.sites[chosen].calls)chosen=i;
      }
      auto &site=s.sites[chosen];
      if(site.calls&&(site.key!=key_||site.kind!=unsigned(phase_)))++s.collisions;
      if(site.key!=key_||site.kind!=unsigned(phase_))site=CpuSite();
      site.key=key_;site.kind=unsigned(phase_);site.ticks+=elapsed*weight_;site.calls+=weight_;
    }
    weight_=0;
  }
};
inline void CpuProfileCount(unsigned index,uint64_t count=1) {
  auto &s=CpuProfileData();if(s.enabled&&index<12)s.counters[index]+=count;
}
inline void CpuProfileDump(const char *phase) {
  auto &s=CpuProfileData();if(!s.enabled||s.top)return;
  static const char *const names[]={"interpreter",
    "hle","io","inflate","io-completion","scheduler","thread-switch","lock",
    "analysis","hash","timing","ge","io-wait"};
  INFO_LOG(BOOT,"cpu-profile phase=%s cumulative stride=%u state-bytes=%u site-evictions=%llu (sampled estimates; wall time includes host preemption)",phase,s.stride,unsigned(sizeof(s)),(unsigned long long)s.collisions);
  for(unsigned i=0;i<unsigned(CpuPhase::Count);++i) {
    const auto &b=s.buckets[i];if(!b.calls)continue;
    INFO_LOG(BOOT,"cpu-profile %s own-us=%llu inclusive-us=%llu calls=%llu samples=%llu",names[i],(unsigned long long)CpuProfileMicros(b.ticks),(unsigned long long)CpuProfileMicros(b.inclusive),(unsigned long long)b.calls,(unsigned long long)b.samples);
  }
  bool used[64]={};
  for(unsigned rank=0;rank<8;++rank) {
    unsigned best=64;for(unsigned i=0;i<64;++i)if(!used[i]&&s.sites[i].calls&&(best==64||s.sites[i].ticks>s.sites[best].ticks))best=i;
    if(best==64)break;
    used[best]=true;const auto &site=s.sites[best];
    INFO_LOG(BOOT,"cpu-profile site kind=%s key=%08x inclusive-us=%llu estimated-calls=%llu",names[site.kind],site.key,(unsigned long long)CpuProfileMicros(site.ticks),(unsigned long long)site.calls);
  }
  INFO_LOG(BOOT,"cpu-profile interpreter-main=%llu delay-slot=%llu io-requested=%llu io-cache-served=%llu io-physical=%llu",(unsigned long long)s.counters[5],(unsigned long long)s.counters[6],(unsigned long long)s.counters[7],(unsigned long long)s.counters[8],(unsigned long long)s.counters[9]);
}
