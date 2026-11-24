#include <cstdio>

using FilePtr = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

void read_data(const char* path) {
    FilePtr file(std::fopen(path, "rb"), &std::fclose);
    if (!file) {
        return; // Failed to open; do not call ferror(nullptr)
    }

    int ch;
    while ((ch = std::fgetc(file.get())) != EOF) {
        // Process character
    }

    if (std::ferror(file.get())) {
        std::perror("Read error occurred");
    } else if (std::feof(file.get())) {
        // Reached end of file normally
    }
}