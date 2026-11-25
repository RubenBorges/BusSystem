module;

export module DatabaseCredentials;
import std;
export import EventType;
export import Pqxx;

export {
struct DatabaseCredentials {
    std::string dbname;
    std::string user;
    std::string password;
    std::string hostaddr;
    std::string port;

    [[nodiscard]] std::string connection_string() const {
        return std::format("dbname={} user={} password={} hostaddr={} port={}",
                           dbname, user, password, hostaddr, port);
    }
};
}
