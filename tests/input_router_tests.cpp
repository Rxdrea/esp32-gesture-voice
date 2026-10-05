#include "../GestureVoice/InputRouter.h"
// 需明确允许运行原始识别器时再启用集成测试。
#ifdef INPUT_ROUTER_WITH_GESTURE_CORE
#include "../GestureVoice/GestureCore.h"
#endif

#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#include <stdexcept>
#define ROUTER_TEST_EXCEPTIONS 1
#endif

namespace {
int failures = 0;
int assertions = 0;
int tests = 0;

void check(bool condition, const char* expression, int line) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
  }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

// 仅替代硬件和网络边界；所有派发均经过真实 InputRouter。
struct Port {
  bool calibration = true;
  bool healthy = true;
  bool ready = true;
  bool online = true;
  bool pinyin = false;
  bool sentence = false;
  bool failOperation = false;
  bool outputProduced = false;
  int queries = 0;
  int readyQueries = 0;
  int networkQueries = 0;
  int availabilityQueries = 0;
  char queriedKey = 0;
  std::vector<std::string> calls;
  std::function<void(const char*)> hook;

  bool calibrated() { ++queries; return calibration; }
  bool inputHealthy() { ++queries; return healthy; }
  bool serialReady() { ++queries; ++readyQueries; return ready; }
  bool needsNetwork(char key) {
    ++queries;
    ++networkQueries;
    queriedKey = key;
    return (key == 'x' && pinyin) || (key == ' ' && sentence);
  }
  bool networkAvailable() { ++queries; ++availabilityQueries; return online; }
  void record(const char* name) {
    calls.push_back(name);
    if (hook) hook(name);
  }
  void beginBusy() { record("begin"); }
  void endBusy() { record("end"); }
  void prepareManualAction() { record("prepare"); }
  void up() { record("up"); }
  void down() { record("down"); }
  void confirm() {
    record("confirm");
    if (failOperation) return;
    if (sentence) outputProduced = true;
  }
  void requestCandidates() {
    record("candidates");
    if (failOperation) return;
    if (pinyin) outputProduced = true;
  }
  void separator() { record("separator"); }
  void back() { record("back"); }
  void unavailableNetwork() { record("offline"); }
};

typedef merged::InputRouter<Port> Router;

void expectCalls(const Port& port, std::initializer_list<const char*> expected) {
  std::vector<std::string> values;
  for (const char* value : expected) values.push_back(value);
  CHECK(port.calls == values);
}

struct Mapping { char key; const char* action; };
const Mapping mappings[] = {
  {'w', "up"}, {'s', "down"}, {' ', "confirm"},
  {'x', "candidates"}, {'f', "separator"}, {'b', "back"}
};

void six_keys_and_manual_reset_order() {
  for (const Mapping& mapping : mappings) {
    Port port;
    Router router(port);
    port.hook = [&](const char*) { CHECK(!router.busy()); };
    CHECK(router.dispatch(mapping.key, false));
    expectCalls(port, {"prepare", mapping.action});
    CHECK(port.readyQueries == 1);
    CHECK(port.availabilityQueries == 0);
    CHECK(!router.busy());
  }
}

void uppercase_is_normalized_before_network_query() {
  const char upper[] = {'W', 'S', ' ', 'X', 'F', 'B'};
  for (unsigned i = 0; i < sizeof(upper); ++i) {
    Port port;
    Router router(port);
    CHECK(router.dispatch(upper[i], false));
    CHECK(port.queriedKey == mappings[i].key);
    expectCalls(port, {"prepare", mappings[i].action});
  }
}

void unknown_bytes_have_no_effect_or_port_query() {
  for (int value = 0; value <= 255; ++value) {
    const char key = static_cast<char>(value);
    if (std::string("ws xfbWSXFB").find(key) != std::string::npos) continue;
    for (int gesture = 0; gesture != 2; ++gesture) {
      Port port;
      Router router(port);
      CHECK(!router.dispatch(key, gesture != 0));
      CHECK(port.calls.empty());
      CHECK(port.queries == 0);
      CHECK(!router.busy());
    }
  }
}

void uncalibrated_drops_all_keys_from_both_sources() {
  for (const Mapping& mapping : mappings) {
    for (int gesture = 0; gesture != 2; ++gesture) {
      Port port;
      port.calibration = false;
      Router router(port);
      CHECK(!router.dispatch(mapping.key, gesture != 0));
      CHECK(port.calls.empty());
      CHECK(port.networkQueries == 0);
      CHECK(!router.busy());
    }
  }
}

void unhealthy_drops_all_keys_from_both_sources() {
  for (const Mapping& mapping : mappings) {
    for (int gesture = 0; gesture != 2; ++gesture) {
      Port port;
      port.healthy = false;
      Router router(port);
      CHECK(!router.dispatch(mapping.key, gesture != 0));
      CHECK(port.calls.empty());
      CHECK(port.networkQueries == 0);
      CHECK(!router.busy());
    }
  }
}

void manual_input_requires_ready() {
  for (const Mapping& mapping : mappings) {
    Port port;
    port.ready = false;
    Router router(port);
    CHECK(!router.dispatch(mapping.key, false));
    CHECK(port.calls.empty());
    CHECK(port.networkQueries == 0);
  }
}

void gesture_input_does_not_require_or_query_ready() {
  for (const Mapping& mapping : mappings) {
    Port port;
    port.ready = false;
    Router router(port);
    CHECK(router.dispatch(mapping.key, true));
    expectCalls(port, {mapping.action});
    CHECK(port.readyQueries == 0);
  }
}

void offline_network_operations_only_show_unavailable() {
  const char keys[] = {'x', 'X', ' '};
  for (char key : keys) {
    for (int gesture = 0; gesture != 2; ++gesture) {
      Port port;
      port.online = false;
      port.pinyin = port.sentence = true;
      Router router(port);
      CHECK(!router.dispatch(key, gesture != 0));
      expectCalls(port, {"offline"});
      CHECK(port.availabilityQueries == 1);
      CHECK(!port.outputProduced);
      CHECK(!router.busy());
    }
  }
}

void local_actions_work_offline_without_entering_busy() {
  for (const Mapping& mapping : mappings) {
    for (int gesture = 0; gesture != 2; ++gesture) {
      Port port;
      port.online = false;
      Router router(port);
      port.hook = [&](const char*) { CHECK(!router.busy()); };
      CHECK(router.dispatch(mapping.key, gesture != 0));
      if (gesture) expectCalls(port, {mapping.action});
      else expectCalls(port, {"prepare", mapping.action});
      CHECK(port.availabilityQueries == 0);
      CHECK(!router.busy());
    }
  }
}

void network_requirement_is_recomputed_for_each_event() {
  Port port;
  port.online = false;
  Router router(port);
  CHECK(router.dispatch('x', false));
  port.calls.clear();
  port.pinyin = true;
  CHECK(!router.dispatch('x', false));
  expectCalls(port, {"offline"});
  port.calls.clear();
  CHECK(router.dispatch(' ', false));
  expectCalls(port, {"prepare", "confirm"});
  port.calls.clear();
  port.sentence = true;
  CHECK(!router.dispatch(' ', false));
  expectCalls(port, {"offline"});
  CHECK(port.networkQueries == 4);
}

void online_network_operations_have_ordered_busy_lifetime() {
  const Mapping network[] = {{'x', "candidates"}, {' ', "confirm"}};
  for (const Mapping& mapping : network) {
    for (int gesture = 0; gesture != 2; ++gesture) {
      Port port;
      port.pinyin = port.sentence = true;
      Router router(port);
      port.hook = [&](const char* name) {
        CHECK(router.busy() == (std::string(name) != "prepare"));
      };
      CHECK(!router.busy());
      CHECK(router.dispatch(mapping.key, gesture != 0));
      if (gesture) expectCalls(port, {"begin", mapping.action, "end"});
      else expectCalls(port, {"prepare", "begin", mapping.action, "end"});
      CHECK(port.outputProduced);
      CHECK(port.availabilityQueries == 1);
      CHECK(!router.busy());
    }
  }
}

void busy_reentry_drops_every_key_in_begin_operation_and_end() {
  const Mapping network[] = {{'x', "candidates"}, {' ', "confirm"}};
  for (const Mapping& mapping : network) {
    Port port;
    port.pinyin = port.sentence = true;
    Router router(port);
    int blocked = 0;
    port.hook = [&](const char* stage) {
      const std::string name(stage);
      if (name == "prepare") return;
      CHECK(router.busy());
      // 防止错误实现无限递归，仍让真实错误动作被记录和断言捕获。
      static bool injecting = false;
      if (injecting) return;
      injecting = true;
      for (const Mapping& nested : mappings) {
        for (int gesture = 0; gesture != 2; ++gesture) {
          const std::size_t before = port.calls.size();
          const int queries = port.queries;
          CHECK(!router.dispatch(nested.key, gesture != 0));
          CHECK(port.calls.size() == before);
          CHECK(port.queries == queries);
          ++blocked;
        }
      }
      injecting = false;
    };
    CHECK(router.dispatch(mapping.key, false));
    expectCalls(port, {"prepare", "begin", mapping.action, "end"});
    CHECK(blocked == 36);
    CHECK(!router.busy());
  }
}

void failed_void_operation_cleans_busy_without_fabricating_output() {
  const Mapping network[] = {{'x', "candidates"}, {' ', "confirm"}};
  for (const Mapping& mapping : network) {
    for (int gesture = 0; gesture != 2; ++gesture) {
      Port port;
      port.pinyin = port.sentence = true;
      port.failOperation = true;
      Router router(port);
      CHECK(router.dispatch(mapping.key, gesture != 0));
      if (gesture) expectCalls(port, {"begin", mapping.action, "end"});
      else expectCalls(port, {"prepare", "begin", mapping.action, "end"});
      CHECK(!port.outputProduced);
      CHECK(!router.busy());
      port.calls.clear();
      port.failOperation = false;
      CHECK(router.dispatch(mapping.key, gesture != 0));
      CHECK(port.outputProduced);
      CHECK(!router.busy());
    }
  }
}

void successful_operation_allows_next_local_event() {
  Port port;
  port.pinyin = true;
  Router router(port);
  CHECK(router.dispatch('x', false));
  CHECK(!router.busy());
  port.calls.clear();
  CHECK(router.dispatch('b', false));
  expectCalls(port, {"prepare", "back"});
}

#ifdef ROUTER_TEST_EXCEPTIONS
void thrown_operation_cleans_busy_and_does_not_report_success() {
  const Mapping network[] = {{'x', "candidates"}, {' ', "confirm"}};
  for (const Mapping& mapping : network) {
    Port port;
    port.pinyin = port.sentence = true;
    Router router(port);
    port.hook = [&](const char* name) {
      if (std::string(name) == mapping.action) throw std::runtime_error("operation");
    };
    bool caught = false;
    try { router.dispatch(mapping.key, false); }
    catch (const std::runtime_error&) { caught = true; }
    CHECK(caught);
    expectCalls(port, {"prepare", "begin", mapping.action, "end"});
    CHECK(!router.busy());
    CHECK(!port.outputProduced);
    port.hook = std::function<void(const char*)>();
    CHECK(router.dispatch('w', false));
  }
}

void thrown_begin_also_releases_busy() {
  Port port;
  port.pinyin = true;
  Router router(port);
  port.hook = [&](const char* name) {
    if (std::string(name) == "begin") throw std::runtime_error("begin");
  };
  bool caught = false;
  try { router.dispatch('x', false); }
  catch (const std::runtime_error&) { caught = true; }
  CHECK(caught);
  expectCalls(port, {"prepare", "begin", "end"});
  CHECK(!router.busy());
}
#endif

#ifdef INPUT_ROUTER_WITH_GESTURE_CORE
// 使用原始 Recognizer 的真实计时/锁定/回位逻辑，不复制算法。
struct RecognizerPort : Port {
  sixops::Recognizer recognizer;
  bool serialReady() {
    ++queries;
    ++readyQueries;
    return std::string(recognizer.stateName()) == "可以操作";
  }
  void prepareManualAction() {
    recognizer.reset();
    record("prepare");
  }
};
typedef merged::InputRouter<RecognizerPort> RecognizerRouter;

char feed(RecognizerPort& port, uint32_t ms, char posture, float envelope = 0) {
  return port.recognizer.update(sixops::Input{ms, envelope, posture, true, true, true});
}

void rearm(RecognizerPort& port, uint32_t start) {
  for (uint32_t offset = 0; offset <= 300; offset += 60)
    CHECK(feed(port, start + offset, 'n') == 0);
}

void real_recognizer_blocks_manual_input_until_rearmed() {
  RecognizerPort port;
  CHECK(port.recognizer.configure(sixops::Thresholds{10, 5}));
  RecognizerRouter router(port);
  CHECK(!router.dispatch('w', false));
  rearm(port, 0);
  CHECK(router.dispatch('w', false));
  expectCalls(port, {"prepare", "up"});
  port.calls.clear();
  CHECK(!router.dispatch('s', false));
  CHECK(port.calls.empty());
  rearm(port, 360);
  CHECK(router.dispatch('s', false));
  expectCalls(port, {"prepare", "down"});
}

void real_recognizer_emitted_gesture_survives_its_own_lock() {
  const char directions[] = {'w', 's', 'f', 'b'};
  const char* actions[] = {"up", "down", "separator", "back"};
  for (unsigned i = 0; i < sizeof(directions); ++i) {
    RecognizerPort port;
    CHECK(port.recognizer.configure(sixops::Thresholds{10, 5}));
    RecognizerRouter router(port);
    rearm(port, 0);
    CHECK(feed(port, 360, directions[i]) == 0);
    CHECK(feed(port, 420, directions[i]) == 0);
    CHECK(feed(port, 480, directions[i]) == 0);
    const char event = feed(port, 540, directions[i]);
    CHECK(event == directions[i]);
    CHECK(std::string(port.recognizer.stateName()) == "回位并放松");
    CHECK(router.dispatch(event, true));
    expectCalls(port, {actions[i]});
    CHECK(port.readyQueries == 0);
    CHECK(feed(port, 600, directions[i]) == 0);
    CHECK(!router.dispatch('w', false));
  }
}

void real_recognizer_manual_reset_cancels_pending_grip() {
  RecognizerPort port;
  CHECK(port.recognizer.configure(sixops::Thresholds{10, 5}));
  RecognizerRouter router(port);
  rearm(port, 0);
  CHECK(feed(port, 360, 'n', 20) == 0);
  CHECK(feed(port, 420, 'n', 20) == 0);
  CHECK(router.dispatch('f', false));
  expectCalls(port, {"prepare", "separator"});
  port.calls.clear();
  for (uint32_t ms = 480; ms <= 2280; ms += 60)
    CHECK(feed(port, ms, 'n', 20) == 0);
  CHECK(port.calls.empty());
  CHECK(!router.dispatch('b', false));
  rearm(port, 2340);
  CHECK(router.dispatch('b', false));
  expectCalls(port, {"prepare", "back"});
}
#endif

void run(const char* name, void (*test)()) {
  ++tests;
  const int before = failures;
  test();
  std::cout << (before == failures ? "PASS " : "FAIL ") << name << '\n';
}
}  // namespace

int main() {
  run("six keys and manual reset order", six_keys_and_manual_reset_order);
  run("uppercase normalized before query", uppercase_is_normalized_before_network_query);
  run("all unknown bytes rejected", unknown_bytes_have_no_effect_or_port_query);
  run("uncalibrated blocks both sources", uncalibrated_drops_all_keys_from_both_sources);
  run("unhealthy blocks both sources", unhealthy_drops_all_keys_from_both_sources);
  run("manual input requires ready", manual_input_requires_ready);
  run("gesture does not recheck ready", gesture_input_does_not_require_or_query_ready);
  run("offline network action suppressed", offline_network_operations_only_show_unavailable);
  run("offline local actions still work", local_actions_work_offline_without_entering_busy);
  run("network requirement follows port state", network_requirement_is_recomputed_for_each_event);
  run("network busy lifetime ordered", online_network_operations_have_ordered_busy_lifetime);
  run("all reentry blocked during busy", busy_reentry_drops_every_key_in_begin_operation_and_end);
  run("failed void operation releases busy", failed_void_operation_cleans_busy_without_fabricating_output);
  run("successful operation permits next event", successful_operation_allows_next_local_event);
#ifdef ROUTER_TEST_EXCEPTIONS
  run("thrown operation releases busy", thrown_operation_cleans_busy_and_does_not_report_success);
  run("thrown begin releases busy", thrown_begin_also_releases_busy);
#endif
#ifdef INPUT_ROUTER_WITH_GESTURE_CORE
  run("real core manual rearm", real_recognizer_blocks_manual_input_until_rearmed);
  run("real core emitted gesture locked", real_recognizer_emitted_gesture_survives_its_own_lock);
  run("real core manual cancels pending grip", real_recognizer_manual_reset_cancels_pending_grip);
#endif
  std::cout << "RESULT tests=" << tests << " assertions=" << assertions
            << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
