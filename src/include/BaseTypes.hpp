#include <cinttypes>
#include <cstdlib>
#include <string>
#include <vector>
struct EventBase {
  virtual ~EventBase() = default;
};
// Kept for backward compatibility if needed
struct FrameData {
  std::vector<uint8_t> data; // BGR24 packed 데이터
  int width = 0;
  int height = 0;
  int64_t pts = 0;
};

namespace EventStruct {
struct Login : public EventBase {
  virtual ~Login() override {};
  enum class State : uint16_t {
    Success,
    Fail,
    WrongPassword,
    NetworkWrong,
    NetworkTimeout
  };
  std::string id;
  std::string password;
  State State_;
};
struct Sign : public EventBase {
  virtual ~Sign() override {};
  enum class State : uint16_t {
    Success,
    Fail,
    SameID,
    ConfirmPassword,
    FailConfirmPassword,
    NetworkWrong,
    NetworkTimeout
  };
  std::string id;
  std::string password;
  std::string username;
  State State_;
};
struct ReadyFrame : public EventBase {
  virtual ~ReadyFrame() override {};
  FrameData FrameData_;
};
struct Admin {
  enum class State : uint16_t { PermitUser };
};
} // namespace EventStruct