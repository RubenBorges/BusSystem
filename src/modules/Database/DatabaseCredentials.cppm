module;

import std;
export import EventType;
export import Pqxx;

export module DatabaseCredentials;

export namespace credentials {

 struct DatabaseCredentials {
    std::string dbname;
    std::string user;
    std::string password;
    std::string hostaddr;
    std::string port;

    [[nodiscard]] std::string connection_string() const {
        return std::format(
            "dbname={} user={} password={} hostaddr={} port={}",
            dbname, user, password, hostaddr, port
        );
    }
};

 struct ValkeyCredentials {
    std::string host{"127.0.0.1"};
    std::string port{"6379"};
    std::string username{"default"};
    std::string password;
    int db_index{0};

    [[nodiscard]] std::string connection_string() const {
        return std::format("-h {} -p {} --user {} -a {} -n {}", host, port, username, password, db_index);
    }
};

 struct ValkeySslCredentials {
    std::string host{"127.0.0.1"};
    std::string port{"6380"};
    std::string username{"default"};
    std::string password;
    int db_index{0};
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;

    [[nodiscard]] std::string connection_string() const {
        return std::format(
            "-h {} -p {} --user {} -a {} -n {} --tls --cacert {} --cert {} --key {}",
            host, port, username, password, db_index, ca_cert_path, client_cert_path, client_key_path
        );
    }
};

// Free URI functions outside the structs
 [[nodiscard]] std::string to_uri(const ValkeyCredentials& creds, bool redis_compatible = false) {
    return std::format(
        "{}://{}:{}@{}:{}/{}",
        redis_compatible ? "redis" : "valkey",
        creds.username, creds.password, creds.host, creds.port, creds.db_index
    );
}

 [[nodiscard]] std::string to_uri(const ValkeySslCredentials& creds, bool redis_compatible = false) {
    return std::format(
        "{}://{}:{}@{}:{}/{}?ssl_ca_certs={}&ssl_certfile={}&ssl_keyfile={}",
        redis_compatible ? "rediss" : "valkeys",
        creds.username, creds.password, creds.host, creds.port, creds.db_index,
        creds.ca_cert_path, creds.client_cert_path, creds.client_key_path
    );
}

} // namespace credentials
