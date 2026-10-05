#pragma once

namespace merged {

template<class Port>
class InputRouter {
 public:
  explicit InputRouter(Port& port) : port_(port), busy_(false) {}

  // true 仅表示已派发；网络操作是否成功由 Port 处理。
  bool dispatch(char key, bool fromGesture) {
    if (key >= 'A' && key <= 'Z') key = static_cast<char>(key + ('a' - 'A'));
    if (!validKey(key) || busy_) return false;
    if (!port_.calibrated() || !port_.inputHealthy()) return false;
    // 手势事件发出时识别器已锁定，不能再要求回位就绪。
    if (!fromGesture && !port_.serialReady()) return false;

    const bool needsNetwork = port_.needsNetwork(key);
    if (needsNetwork && !port_.networkAvailable()) {
      port_.unavailableNetwork();
      return false;
    }

    if (!fromGesture) port_.prepareManualAction();
    if (needsNetwork) {
      // 先置忙再通知 Port；正常返回或异常展开都会恢复。
      BusyScope scope(*this);
      port_.beginBusy();
      perform(key);
    } else {
      perform(key);
    }
    return true;
  }

  bool busy() const { return busy_; }

 private:
  Port& port_;
  bool busy_;

  class BusyScope {
   public:
    explicit BusyScope(InputRouter& router) : router_(router) {
      router_.busy_ = true;
    }
    ~BusyScope() {
      // endBusy 应不抛异常；清理期间仍拒绝重入。
      router_.port_.endBusy();
      router_.busy_ = false;
    }
    BusyScope(const BusyScope&) = delete;
    BusyScope& operator=(const BusyScope&) = delete;
   private:
    InputRouter& router_;
  };

  static bool validKey(char key) {
    return key == 'w' || key == 's' || key == ' ' ||
           key == 'x' || key == 'f' || key == 'b';
  }

  void perform(char key) {
    switch (key) {
      case 'w': port_.up(); break;
      case 's': port_.down(); break;
      case ' ': port_.confirm(); break;
      case 'x': port_.requestCandidates(); break;
      case 'f': port_.separator(); break;
      case 'b': port_.back(); break;
    }
  }
};

}  // namespace merged
