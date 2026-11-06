#pragma once

#include <atomic>
#include <expected>
#include <string>

namespace messagebus {

struct Message {
  std::string content;
};

class InterfacePolicy {
public:
  virtual ~InterfacePolicy() = default;
  virtual void interface(const Message& message, std::expected<int, std::string>) = 0;
};

class StdIOInterfacer : public InterfacePolicy {
public:
  // Default constructor: uses std::cout and std::cin
  StdIOInterfacer();

  // Custom stream constructor: inject your own target streams
  StdIOInterfacer(std::ostream& out_stream, std::istream& in_stream);

  void interface(const Message& message, std::expected<int, std::string>) override;

private:
  std::ostream* out_; // Pointer to the output destination
  std::istream* in_;  // Pointer to the input source
};


class PipedInterfacer : public InterfacePolicy {
public:
  PipedInterfacer();
  void interface(const Message& message, std::expected<int, std::string>) override;
};


class AF_UnixInterfacer : public InterfacePolicy {
public:
    AF_UnixInterfacer();                         
    AF_UnixInterfacer(const std::string& path);   
    ~AF_UnixInterfacer();                        

    void interface(const Message& message, std::expected<int, std::string> status) override;
    
    int getServerFD() const;
    void listen_and_process_loop();              
    void stop();                                 // ADD THIS LINE

private:
    int server_fd;              
    std::string socket_path;    
    std::atomic<bool> running_{true};
};

class NetworkInterfacer : public InterfacePolicy {
public:
  NetworkInterfacer();
  NetworkInterfacer(const std::string& ip, int port);
  void interface(const Message& message, std::expected<int, std::string>) override;
private:
  std::string ip_;
  int port_;
};

}  // namespace messagebus
