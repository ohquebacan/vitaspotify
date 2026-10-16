#pragma once

#include <stdint.h>  // for uint8_t
#include <memory>    // for shared_ptr, unique_ptr
#include <string>    // for string
#include <vector>    // for vector

namespace cspot {
class AuthChallenges;
class LoginBlob;
class PlainConnection;
class ShannonConnection;
}  // namespace cspot

#define LOGIN_REQUEST_COMMAND 0xAB
#define AUTH_SUCCESSFUL_COMMAND 0xAC
#define AUTH_DECLINED_COMMAND 0xAD

namespace cspot {
class Session {
 protected:
  std::unique_ptr<cspot::AuthChallenges> challenges;
  std::shared_ptr<cspot::PlainConnection> conn;
  std::shared_ptr<LoginBlob> authBlob;

  std::string deviceId = "142137fd329622137a14901634264e6f332e2411";

 public:
  Session();
  ~Session();

  std::shared_ptr<cspot::ShannonConnection> shanConn;

  // The name the access point says this account really has, from APWelcome.
  // It is the only place a token login can learn it: the login itself carried
  // no username, and the saved credentials are useless without one, since a
  // stored-credential login sends the name alongside them.
  std::string canonicalUsername;

  void connect(std::unique_ptr<cspot::PlainConnection> connection);
  void connectWithRandomAp();
  void close();
  virtual bool triggerTimeout() = 0;
  std::vector<uint8_t> authenticate(std::shared_ptr<LoginBlob> blob);
};
}  // namespace cspot
