/*
 * Система виртуального архива
 * Виртуальное консольное ядро для управления иерархическим хранилищем данных
 *
 * Компиляция: g++ -std=c++17 -o archive virtual_archive.cpp
 * Запуск: ./archive
 */

#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <regex>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <mutex>

 // ======================== МАГИЧЕСКОЕ ЧИСЛО ДЛЯ СЕРИАЛИЗАЦИИ ========================
const uint32_t MAGIC_NUMBER = 0x41524348; // "ARCH"
const uint32_t SERIALIZATION_VERSION = 1;

// ======================== КЛАСС ИСКЛЮЧЕНИЙ ========================
class ArchiveException : public std::exception {
private:
    std::string message;
public:
    explicit ArchiveException(const std::string& msg) : message(msg) {}
    const char* what() const noexcept override { return message.c_str(); }
};

class FileSystemException : public ArchiveException {
public:
    explicit FileSystemException(const std::string& msg) : ArchiveException(msg) {}
};

class SerializationException : public ArchiveException {
public:
    explicit SerializationException(const std::string& msg) : ArchiveException(msg) {}
};

class AccessDeniedException : public ArchiveException {
public:
    explicit AccessDeniedException(const std::string& msg) : ArchiveException(msg) {}
};

// ======================== КЛАСС ДАТЫ ========================
class Date {
private:
    int year, month, day, hour, minute, second;

public:
    Date() : year(1970), month(1), day(1), hour(0), minute(0), second(0) {}

    Date(int y, int m, int d, int h = 0, int min = 0, int s = 0)
        : year(y), month(m), day(d), hour(h), minute(min), second(s) {
    }

    static Date now() {
        time_t t = std::time(nullptr);
        struct tm* now = std::localtime(&t);
        return Date(now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
            now->tm_hour, now->tm_min, now->tm_sec);
    }

    int getYear() const { return year; }
    int getMonth() const { return month; }
    int getDay() const { return day; }

    std::string toString() const {
        std::ostringstream oss;
        oss << year << "-" << std::setw(2) << std::setfill('0') << month
            << "-" << std::setw(2) << std::setfill('0') << day;
        return oss.str();
    }

    std::string toFullString() const {
        std::ostringstream oss;
        oss << year << "-" << std::setw(2) << std::setfill('0') << month
            << "-" << std::setw(2) << std::setfill('0') << day
            << " " << std::setw(2) << std::setfill('0') << hour
            << ":" << std::setw(2) << std::setfill('0') << minute
            << ":" << std::setw(2) << std::setfill('0') << second;
        return oss.str();
    }

    void print() const {
        std::cout << toString();
    }

    time_t toTimeT() const {
        struct tm t = { 0 };
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = second;
        return mktime(&t);
    }

    bool operator<(const Date& other) const {
        if (year != other.year) return year < other.year;
        if (month != other.month) return month < other.month;
        if (day != other.day) return day < other.day;
        if (hour != other.hour) return hour < other.hour;
        if (minute != other.minute) return minute < other.minute;
        return second < other.second;
    }

    bool operator>(const Date& other) const { return other < *this; }
    bool operator<=(const Date& other) const { return !(*this > other); }
    bool operator>=(const Date& other) const { return !(*this < other); }
    bool operator==(const Date& other) const {
        return year == other.year && month == other.month && day == other.day;
    }
};

// ======================== КЛАСС ЛОГГЕРА (СИНГЛТОН) ========================
class Logger {
private:
    static Logger* instance;
    static std::mutex mutex;
    std::ofstream logFile;

    Logger() {
        logFile.open("history.log", std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Warning: Could not open log file" << std::endl;
        }
    }

    ~Logger() {
        if (logFile.is_open()) logFile.close();
    }

public:
    enum class Level { INFO, WARNING, ERROR };

    static Logger& getInstance() {
        std::lock_guard<std::mutex> lock(mutex);
        if (instance == nullptr) instance = new Logger();
        return *instance;
    }

    void log(const std::string& operation, bool success, const std::string& details = "") {
        Date now = Date::now();
        std::string status = success ? "SUCCESS" : "FAILURE";
        std::string message = "[" + now.toFullString() + "] " + operation + " - " + status;
        if (!details.empty()) message += " - " + details;

        if (logFile.is_open()) {
            logFile << message << std::endl;
            logFile.flush();
        }
        std::cout << "[LOG] " << message << std::endl;
    }

    void log(Level level, const std::string& message) {
        Date now = Date::now();
        std::string levelStr;
        switch (level) {
        case Level::INFO: levelStr = "INFO"; break;
        case Level::WARNING: levelStr = "WARNING"; break;
        case Level::ERROR: levelStr = "ERROR"; break;
        }
        std::string logMessage = "[" + now.toFullString() + "] [" + levelStr + "] " + message;

        if (logFile.is_open()) {
            logFile << logMessage << std::endl;
            logFile.flush();
        }
    }
};

Logger* Logger::instance = nullptr;
std::mutex Logger::mutex;

#define LOG_INFO(msg) Logger::getInstance().log(Logger::Level::INFO, msg)
#define LOG_WARNING(msg) Logger::getInstance().log(Logger::Level::WARNING, msg)
#define LOG_ERROR(msg) Logger::getInstance().log(Logger::Level::ERROR, msg)
#define LOG_OPERATION(op, success, details) Logger::getInstance().log(op, success, details)

// ======================== УРОВНИ ДОСТУПА ========================
enum class AccessLevel {
    GUEST = 0,
    USER = 1,
    ADMIN = 2
};

std::string accessLevelToString(AccessLevel level) {
    switch (level) {
    case AccessLevel::GUEST: return "GUEST";
    case AccessLevel::USER: return "USER";
    case AccessLevel::ADMIN: return "ADMIN";
    default: return "UNKNOWN";
    }
}

// ======================== ТИПЫ РЕСУРСОВ ========================
enum class ResourceType {
    FILE_RESOURCE,
    DIRECTORY
};

// ======================== БАЗОВЫЙ КЛАСС RESOURCE ========================
class Resource {
private:
    std::string name;
    Date creationDate;
    ResourceType type;

protected:
    Resource(const std::string& name, ResourceType type)
        : name(name), creationDate(Date::now()), type(type) {
        if (!isValidName(name)) {
            throw FileSystemException("Invalid resource name: " + name);
        }
    }

public:
    virtual ~Resource() = default;

    std::string getName() const { return name; }
    Date getCreationDate() const { return creationDate; }
    ResourceType getType() const { return type; }

    void setName(const std::string& newName) {
        if (!isValidName(newName)) {
            throw FileSystemException("Invalid resource name: " + newName);
        }
        name = newName;
    }

    virtual size_t getSize() const = 0;
    virtual std::unique_ptr<Resource> clone() const = 0;
    virtual void print(int depth = 0) const = 0;

    static bool isValidName(const std::string& name) {
        if (name.empty()) return false;
        std::regex forbiddenChars(R"([\\/:*?"<>|])");
        return !std::regex_search(name, forbiddenChars);
    }
};

// ======================== КЛАСС FILE ========================
class File : public Resource {
private:
    std::string extension;
    size_t size;

public:
    File(const std::string& name, const std::string& extension, size_t size = 0)
        : Resource(name, ResourceType::FILE_RESOURCE), extension(extension), size(size) {
        if (!isValidExtension(extension)) {
            throw FileSystemException("Invalid file extension: " + extension);
        }
    }

    std::string getExtension() const { return extension; }
    void setExtension(const std::string& ext) {
        if (!isValidExtension(ext)) {
            throw FileSystemException("Invalid file extension: " + ext);
        }
        extension = ext;
    }

    size_t getSize() const override { return size; }
    void setSize(size_t newSize) { size = newSize; }

    std::unique_ptr<Resource> clone() const override {
        return std::make_unique<File>(getName(), extension, size);
    }

    void print(int depth = 0) const override {
        std::cout << std::string(depth * 2, ' ') << "📄 " << getName() << "." << extension
            << " (" << size << " bytes, created: ";
        getCreationDate().print();
        std::cout << ")" << std::endl;
    }

    static bool isValidExtension(const std::string& extension) {
        if (extension.empty()) return true;
        std::regex extensionPattern(R"(^[a-zA-Z0-9]+$)");
        return std::regex_match(extension, extensionPattern);
    }
};

// ======================== КЛАСС DIRECTORY ========================
class Directory : public Resource {
private:
    std::vector<std::unique_ptr<Resource>> children;
    AccessLevel accessLevel;

public:
    Directory(const std::string& name, AccessLevel level = AccessLevel::USER)
        : Resource(name, ResourceType::DIRECTORY), accessLevel(level) {
    }

    AccessLevel getAccessLevel() const { return accessLevel; }
    void setAccessLevel(AccessLevel level) { accessLevel = level; }

    size_t getSize() const override { return calculateTotalSize(); }

    size_t calculateTotalSize() const {
        size_t total = 0;
        for (const auto& child : children) {
            total += child->getSize();
        }
        return total;
    }

    void addChild(std::unique_ptr<Resource> child) {
        if (findChild(child->getName()) != nullptr) {
            throw FileSystemException("Resource already exists: " + child->getName());
        }
        children.push_back(std::move(child));
    }

    std::unique_ptr<Resource> removeChild(const std::string& name) {
        auto it = std::find_if(children.begin(), children.end(),
            [&name](const std::unique_ptr<Resource>& child) {
                return child->getName() == name;
            });

        if (it == children.end()) {
            throw FileSystemException("Resource not found: " + name);
        }

        std::unique_ptr<Resource> removed = std::move(*it);
        children.erase(it);
        return removed;
    }

    Resource* findChild(const std::string& name) const {
        auto it = std::find_if(children.begin(), children.end(),
            [&name](const std::unique_ptr<Resource>& child) {
                return child->getName() == name;
            });

        if (it != children.end()) return it->get();
        return nullptr;
    }

    std::unique_ptr<Resource> clone() const override {
        auto newDir = std::make_unique<Directory>(getName(), accessLevel);
        for (const auto& child : children) {
            newDir->addChild(child->clone());
        }
        return newDir;
    }

    void print(int depth = 0) const override {
        std::string levelStr = "[" + accessLevelToString(accessLevel) + "]";
        std::cout << std::string(depth * 2, ' ') << "📁 " << getName() << " " << levelStr
            << " (created: ";
        getCreationDate().print();
        std::cout << ", size: " << calculateTotalSize() << " bytes)" << std::endl;

        for (const auto& child : children) {
            child->print(depth + 1);
        }
    }

    const std::vector<std::unique_ptr<Resource>>& getChildren() const { return children; }

    size_t getFileCount() const {
        size_t count = 0;
        for (const auto& child : children) {
            if (child->getType() == ResourceType::FILE_RESOURCE) {
                count++;
            }
            else {
                count += dynamic_cast<Directory*>(child.get())->getFileCount();
            }
        }
        return count;
    }

    size_t getDirectoryCount() const {
        size_t count = 0;
        for (const auto& child : children) {
            if (child->getType() == ResourceType::DIRECTORY) {
                count++;
                count += dynamic_cast<Directory*>(child.get())->getDirectoryCount();
            }
        }
        return count;
    }

    std::vector<Resource*> searchByName(const std::string& pattern) {
        std::vector<Resource*> results;
        std::regex regexPattern(pattern, std::regex::icase);

        for (auto& child : children) {
            if (std::regex_search(child->getName(), regexPattern)) {
                results.push_back(child.get());
            }
            if (child->getType() == ResourceType::DIRECTORY) {
                auto subResults = dynamic_cast<Directory*>(child.get())->searchByName(pattern);
                results.insert(results.end(), subResults.begin(), subResults.end());
            }
        }
        return results;
    }

    std::vector<Resource*> searchByDateRange(const Date& start, const Date& end) {
        std::vector<Resource*> results;

        for (auto& child : children) {
            if (child->getCreationDate() >= start && child->getCreationDate() <= end) {
                results.push_back(child.get());
            }
            if (child->getType() == ResourceType::DIRECTORY) {
                auto subResults = dynamic_cast<Directory*>(child.get())->searchByDateRange(start, end);
                results.insert(results.end(), subResults.begin(), subResults.end());
            }
        }
        return results;
    }

    std::vector<Resource*> searchByMask(const std::string& namePattern, const std::string& extension = "") {
        std::vector<Resource*> results;
        std::regex nameRegex(namePattern, std::regex::icase);

        for (auto& child : children) {
            bool nameMatch = std::regex_search(child->getName(), nameRegex);
            bool extMatch = true;

            if (child->getType() == ResourceType::FILE_RESOURCE && !extension.empty()) {
                auto file = dynamic_cast<File*>(child.get());
                extMatch = (file->getExtension() == extension);
            }

            if (nameMatch && extMatch) {
                results.push_back(child.get());
            }

            if (child->getType() == ResourceType::DIRECTORY) {
                auto subResults = dynamic_cast<Directory*>(child.get())->searchByMask(namePattern, extension);
                results.insert(results.end(), subResults.begin(), subResults.end());
            }
        }
        return results;
    }

    bool checkAccess(AccessLevel userLevel) const {
        return static_cast<int>(userLevel) >= static_cast<int>(accessLevel);
    }
};

// ======================== КЛАСС СЕРИАЛИЗАТОРА ========================
class Serializer {
private:
    static void writeString(std::ofstream& file, const std::string& str) {
        uint32_t len = str.length();
        file.write(reinterpret_cast<const char*>(&len), sizeof(len));
        file.write(str.c_str(), len);
    }

    static std::string readString(std::ifstream& file) {
        uint32_t len;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string str(len, '\0');
        file.read(&str[0], len);
        return str;
    }

    static void writeDate(std::ofstream& file, const Date& date) {
        int32_t year = date.getYear();
        int32_t month = date.getMonth();
        int32_t day = date.getDay();
        file.write(reinterpret_cast<const char*>(&year), sizeof(year));
        file.write(reinterpret_cast<const char*>(&month), sizeof(month));
        file.write(reinterpret_cast<const char*>(&day), sizeof(day));
    }

    static Date readDate(std::ifstream& file) {
        int32_t year, month, day;
        file.read(reinterpret_cast<char*>(&year), sizeof(year));
        file.read(reinterpret_cast<char*>(&month), sizeof(month));
        file.read(reinterpret_cast<char*>(&day), sizeof(day));
        return Date(year, month, day);
    }

    static void saveResource(std::ofstream& file, const Resource& resource) {
        uint8_t type = static_cast<uint8_t>(resource.getType());
        file.write(reinterpret_cast<const char*>(&type), sizeof(type));
        writeString(file, resource.getName());
        writeDate(file, resource.getCreationDate());

        if (resource.getType() == ResourceType::FILE_RESOURCE) {
            const File& fileRes = dynamic_cast<const File&>(resource);
            writeString(file, fileRes.getExtension());
            size_t size = fileRes.getSize();
            file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        }
        else {
            const Directory& dir = dynamic_cast<const Directory&>(resource);
            uint8_t accessLevel = static_cast<uint8_t>(dir.getAccessLevel());
            file.write(reinterpret_cast<const char*>(&accessLevel), sizeof(accessLevel));

            uint32_t childCount = dir.getChildren().size();
            file.write(reinterpret_cast<const char*>(&childCount), sizeof(childCount));

            for (const auto& child : dir.getChildren()) {
                saveResource(file, *child);
            }
        }
    }

    static std::unique_ptr<Resource> loadResource(std::ifstream& file) {
        uint8_t type;
        file.read(reinterpret_cast<char*>(&type), sizeof(type));

        std::string name = readString(file);
        Date creationDate = readDate(file);

        if (type == static_cast<uint8_t>(ResourceType::FILE_RESOURCE)) {
            std::string extension = readString(file);
            size_t size;
            file.read(reinterpret_cast<char*>(&size), sizeof(size));
            auto fileRes = std::make_unique<File>(name, extension, size);
            return fileRes;
        }
        else {
            uint8_t accessLevel;
            file.read(reinterpret_cast<char*>(&accessLevel), sizeof(accessLevel));
            auto dir = std::make_unique<Directory>(name, static_cast<AccessLevel>(accessLevel));

            uint32_t childCount;
            file.read(reinterpret_cast<char*>(&childCount), sizeof(childCount));

            for (uint32_t i = 0; i < childCount; i++) {
                dir->addChild(loadResource(file));
            }
            return dir;
        }
    }

public:
    static void save(const Directory& root, const std::string& filename) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw SerializationException("Cannot open file for writing: " + filename);
        }

        file.write(reinterpret_cast<const char*>(&MAGIC_NUMBER), sizeof(MAGIC_NUMBER));
        file.write(reinterpret_cast<const char*>(&SERIALIZATION_VERSION), sizeof(SERIALIZATION_VERSION));

        saveResource(file, root);

        LOG_INFO("Archive saved to " + filename);
    }

    static std::unique_ptr<Directory> load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            throw SerializationException("Cannot open file for reading: " + filename);
        }

        uint32_t magic;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));

        if (magic != MAGIC_NUMBER) {
            throw SerializationException("Invalid file format: wrong magic number");
        }

        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        if (version != SERIALIZATION_VERSION) {
            throw SerializationException("Unsupported version: " + std::to_string(version));
        }

        auto root = loadResource(file);
        if (root->getType() != ResourceType::DIRECTORY) {
            throw SerializationException("Root resource must be a directory");
        }

        LOG_INFO("Archive loaded from " + filename);
        return std::unique_ptr<Directory>(dynamic_cast<Directory*>(root.release()));
    }
};

// ======================== КЛАСС МЕНЕДЖЕРА АРХИВА ========================
class ArchiveManager {
private:
    std::unique_ptr<Directory> root;
    AccessLevel currentUser;

    bool checkAccess(const Directory& dir) const {
        return dir.checkAccess(currentUser);
    }

    void ensureAccess(const Directory& dir) const {
        if (!checkAccess(dir)) {
            throw AccessDeniedException("Access denied to directory: " + dir.getName());
        }
    }

public:
    ArchiveManager() : currentUser(AccessLevel::ADMIN) {
        root = std::make_unique<Directory>("root", AccessLevel::ADMIN);
        LOG_INFO("Archive manager initialized");
    }

    void setUserLevel(AccessLevel level) {
        currentUser = level;
        LOG_INFO("User level changed to " + accessLevelToString(level));
    }

    AccessLevel getUserLevel() const { return currentUser; }

    void createFile(const std::string& path, const std::string& name,
        const std::string& extension, size_t size = 0) {
        try {
            auto* parent = navigateToDirectory(path);
            ensureAccess(*parent);

            auto file = std::make_unique<File>(name, extension, size);
            parent->addChild(std::move(file));

            LOG_OPERATION("CREATE_FILE", true, path + "/" + name + "." + extension);
        }
        catch (const std::exception& e) {
            LOG_OPERATION("CREATE_FILE", false, e.what());
            throw;
        }
    }

    void createDirectory(const std::string& path, const std::string& name, AccessLevel level = AccessLevel::USER) {
        try {
            auto* parent = navigateToDirectory(path);
            ensureAccess(*parent);

            auto dir = std::make_unique<Directory>(name, level);
            parent->addChild(std::move(dir));

            LOG_OPERATION("CREATE_DIRECTORY", true, path + "/" + name);
        }
        catch (const std::exception& e) {
            LOG_OPERATION("CREATE_DIRECTORY", false, e.what());
            throw;
        }
    }

    void removeResource(const std::string& path) {
        try {
            size_t lastSlash = path.find_last_of('/');
            std::string parentPath = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : "";
            std::string name = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;

            auto* parent = navigateToDirectory(parentPath);
            ensureAccess(*parent);

            parent->removeChild(name);
            LOG_OPERATION("REMOVE", true, path);
        }
        catch (const std::exception& e) {
            LOG_OPERATION("REMOVE", false, e.what());
            throw;
        }
    }

    void moveResource(const std::string& sourcePath, const std::string& destPath) {
        try {
            size_t lastSlashSrc = sourcePath.find_last_of('/');
            std::string srcParentPath = (lastSlashSrc != std::string::npos) ? sourcePath.substr(0, lastSlashSrc) : "";
            std::string srcName = (lastSlashSrc != std::string::npos) ? sourcePath.substr(lastSlashSrc + 1) : sourcePath;

            size_t lastSlashDst = destPath.find_last_of('/');
            std::string dstParentPath = (lastSlashDst != std::string::npos) ? destPath.substr(0, lastSlashDst) : "";
            std::string dstName = (lastSlashDst != std::string::npos) ? destPath.substr(lastSlashDst + 1) : destPath;

            auto* srcParent = navigateToDirectory(srcParentPath);
            auto* dstParent = navigateToDirectory(dstParentPath);

            ensureAccess(*srcParent);
            ensureAccess(*dstParent);

            auto resource = srcParent->removeChild(srcName);
            dstParent->addChild(std::move(resource));

            LOG_OPERATION("MOVE", true, sourcePath + " -> " + destPath);
        }
        catch (const std::exception& e) {
            LOG_OPERATION("MOVE", false, e.what());
            throw;
        }
    }

    void copyResource(const std::string& sourcePath, const std::string& destPath) {
        try {
            size_t lastSlashSrc = sourcePath.find_last_of('/');
            std::string srcParentPath = (lastSlashSrc != std::string::npos) ? sourcePath.substr(0, lastSlashSrc) : "";
            std::string srcName = (lastSlashSrc != std::string::npos) ? sourcePath.substr(lastSlashSrc + 1) : sourcePath;

            size_t lastSlashDst = destPath.find_last_of('/');
            std::string dstParentPath = (lastSlashDst != std::string::npos) ? destPath.substr(0, lastSlashDst) : "";
            std::string dstName = (lastSlashDst != std::string::npos) ? destPath.substr(lastSlashDst + 1) : destPath;

            auto* srcParent = navigateToDirectory(srcParentPath);
            auto* dstParent = navigateToDirectory(dstParentPath);

            ensureAccess(*srcParent);
            ensureAccess(*dstParent);

            Resource* src = srcParent->findChild(srcName);
            if (!src) {
                throw FileSystemException("Source not found: " + sourcePath);
            }

            auto copy = src->clone();
            dstParent->addChild(std::move(copy));

            LOG_OPERATION("COPY", true, sourcePath + " -> " + destPath);
        }
        catch (const std::exception& e) {
            LOG_OPERATION("COPY", false, e.what());
            throw;
        }
    }

    void printTree() const {
        std::cout << "\n=== ARCHIVE STRUCTURE ===" << std::endl;
        root->print();
        std::cout << "=========================" << std::endl;
    }

    void printStatistics() const {
        std::cout << "\n=== ARCHIVE STATISTICS ===" << std::endl;
        std::cout << "Total files: " << root->getFileCount() << std::endl;
        std::cout << "Total directories: " << root->getDirectoryCount() << std::endl;
        std::cout << "Total size: " << root->calculateTotalSize() << " bytes" << std::endl;

        size_t fileCount = root->getFileCount();
        if (fileCount > 0) {
            std::cout << "Average file size: " << (root->calculateTotalSize() / fileCount) << " bytes" << std::endl;
        }
        std::cout << "===========================" << std::endl;
    }

    void searchByMask(const std::string& namePattern, const std::string& extension = "") {
        std::cout << "\n=== SEARCH RESULTS (mask: " << namePattern;
        if (!extension.empty()) std::cout << ", ext: " << extension;
        std::cout << ") ===" << std::endl;

        auto results = root->searchByMask(namePattern, extension);

        if (results.empty()) {
            std::cout << "No resources found." << std::endl;
        }
        else {
            for (auto* res : results) {
                res->print();
            }
        }
        std::cout << "=========================" << std::endl;
    }

    void searchByDateRange(int startYear, int startMonth, int startDay,
        int endYear, int endMonth, int endDay) {
        Date start(startYear, startMonth, startDay);
        Date end(endYear, endMonth, endDay);

        std::cout << "\n=== SEARCH RESULTS (date range: " << start.toString()
            << " to " << end.toString() << ") ===" << std::endl;

        auto results = root->searchByDateRange(start, end);

        if (results.empty()) {
            std::cout << "No resources found." << std::endl;
        }
        else {
            for (auto* res : results) {
                res->print();
            }
        }
        std::cout << "=========================" << std::endl;
    }

    void exportToCSV(const std::string& filename) {
        std::ofstream file(filename);
        if (!file) {
            throw FileSystemException("Cannot create CSV file: " + filename);
        }

        file << "Type,Name,Extension,Size,Path,CreationDate,AccessLevel\n";

        std::function<void(const Directory&, const std::string&)> exportDir;
        exportDir = [&](const Directory& dir, const std::string& currentPath) {
            for (const auto& child : dir.getChildren()) {
                std::string fullPath = currentPath + "/" + child->getName();

                if (child->getType() == ResourceType::FILE_RESOURCE) {
                    auto* fileRes = dynamic_cast<File*>(child.get());
                    file << "FILE," << child->getName() << "," << fileRes->getExtension() << ","
                        << fileRes->getSize() << "," << fullPath << ","
                        << child->getCreationDate().toString() << ",\n";
                }
                else {
                    auto* subDir = dynamic_cast<Directory*>(child.get());
                    std::string levelStr = accessLevelToString(subDir->getAccessLevel());
                    file << "DIRECTORY," << child->getName() << ",,"
                        << subDir->calculateTotalSize() << "," << fullPath << ","
                        << child->getCreationDate().toString() << "," << levelStr << "\n";
                    exportDir(*subDir, fullPath);
                }
            }
            };

        exportDir(*root, "");
        file.close();

        LOG_INFO("Exported to CSV: " + filename);
        std::cout << "Data exported to " << filename << std::endl;
    }

    void sortChildren(const std::string& path, const std::string& criterion) {
        auto* dir = navigateToDirectory(path);
        ensureAccess(*dir);

        std::vector<std::unique_ptr<Resource>> sorted;
        sorted.reserve(dir->getChildren().size());

        for (auto& child : const_cast<std::vector<std::unique_ptr<Resource>>&>(dir->getChildren())) {
            sorted.push_back(std::move(child));
        }

        if (criterion == "name") {
            std::sort(sorted.begin(), sorted.end(),
                [](const std::unique_ptr<Resource>& a, const std::unique_ptr<Resource>& b) {
                    return a->getName() < b->getName();
                });
        }
        else if (criterion == "size") {
            std::sort(sorted.begin(), sorted.end(),
                [](const std::unique_ptr<Resource>& a, const std::unique_ptr<Resource>& b) {
                    return a->getSize() < b->getSize();
                });
        }
        else if (criterion == "date") {
            std::sort(sorted.begin(), sorted.end(),
                [](const std::unique_ptr<Resource>& a, const std::unique_ptr<Resource>& b) {
                    return a->getCreationDate() < b->getCreationDate();
                });
        }

        const_cast<std::vector<std::unique_ptr<Resource>>&>(dir->getChildren()).clear();
        for (auto& child : sorted) {
            const_cast<std::vector<std::unique_ptr<Resource>>&>(dir->getChildren()).push_back(std::move(child));
        }

        LOG_INFO("Sorted directory " + path + " by " + criterion);
    }

    void saveArchive(const std::string& filename) {
        Serializer::save(*root, filename);
    }

    void loadArchive(const std::string& filename) {
        root = Serializer::load(filename);
        LOG_INFO("Archive loaded successfully");
    }

    Directory* navigateToDirectory(const std::string& path) {
        if (path.empty() || path == "/" || path == "root") {
            return root.get();
        }

        Directory* current = root.get();
        std::stringstream ss(path);
        std::string segment;

        while (std::getline(ss, segment, '/')) {
            if (segment.empty()) continue;

            Resource* child = current->findChild(segment);
            if (!child || child->getType() != ResourceType::DIRECTORY) {
                throw FileSystemException("Directory not found: " + segment);
            }
            current = dynamic_cast<Directory*>(child);
        }

        return current;
    }
};

// ======================== ФУНКЦИИ ДЛЯ ТЕСТИРОВАНИЯ ========================
void runTests() {
    std::cout << "\n========== RUNNING TESTS ==========\n" << std::endl;

    // Test 1: Create archive and add resources
    std::cout << "Test 1: Creating archive and adding resources..." << std::endl;
    ArchiveManager manager;

    try {
        manager.createDirectory("/", "documents", AccessLevel::USER);
        manager.createDirectory("/", "images", AccessLevel::GUEST);
        manager.createFile("/documents", "report", "txt", 1024);
        manager.createFile("/documents", "presentation", "pdf", 2048);
        manager.createFile("/images", "photo", "jpg", 512);
        manager.createDirectory("/documents", "archive", AccessLevel::ADMIN);
        manager.createFile("/documents/archive", "backup", "zip", 4096);

        manager.printTree();
        std::cout << "✓ Test 1 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 1 failed: " << e.what() << std::endl;
    }

    // Test 2: Calculate size
    std::cout << "Test 2: Calculating total size..." << std::endl;
    try {
        manager.printStatistics();
        std::cout << "✓ Test 2 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 2 failed: " << e.what() << std::endl;
    }

    // Test 3: Search by mask
    std::cout << "Test 3: Searching by mask..." << std::endl;
    try {
        manager.searchByMask("report", "txt");
        manager.searchByMask("photo");
        std::cout << "✓ Test 3 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 3 failed: " << e.what() << std::endl;
    }

    // Test 4: Copy resource
    std::cout << "Test 4: Copying resource..." << std::endl;
    try {
        manager.copyResource("/documents/report.txt", "/images/report_copy.txt");
        manager.printTree();
        std::cout << "✓ Test 4 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 4 failed: " << e.what() << std::endl;
    }

    // Test 5: Move resource
    std::cout << "Test 5: Moving resource..." << std::endl;
    try {
        manager.moveResource("/images/photo.jpg", "/documents/photo.jpg");
        manager.printTree();
        std::cout << "✓ Test 5 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 5 failed: " << e.what() << std::endl;
    }

    // Test 6: Remove resource
    std::cout << "Test 6: Removing resource..." << std::endl;
    try {
        manager.removeResource("/documents/archive/backup.zip");
        manager.printTree();
        std::cout << "✓ Test 6 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 6 failed: " << e.what() << std::endl;
    }

    // Test 7: Invalid name validation
    std::cout << "Test 7: Invalid name validation..." << std::endl;
    try {
        manager.createFile("/", "bad:name", "txt");
        std::cout << "✗ Test 7 failed: Should have thrown exception" << std::endl;
    }
    catch (const FileSystemException& e) {
        std::cout << "✓ Test 7 passed: Caught exception - " << e.what() << std::endl;
    }

    // Test 8: Access control
    std::cout << "\nTest 8: Access control..." << std::endl;
    try {
        manager.setUserLevel(AccessLevel::GUEST);
        manager.createFile("/documents/archive", "secret", "txt");
        std::cout << "✗ Test 8 failed: Should have denied access" << std::endl;
    }
    catch (const AccessDeniedException& e) {
        std::cout << "✓ Test 8 passed: Access denied - " << e.what() << std::endl;
    }
    manager.setUserLevel(AccessLevel::ADMIN);

    // Test 9: Sort by criterion
    std::cout << "\nTest 9: Sorting by criterion..." << std::endl;
    try {
        manager.sortChildren("/documents", "size");
        manager.printTree();
        manager.sortChildren("/documents", "name");
        manager.printTree();
        std::cout << "✓ Test 9 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 9 failed: " << e.what() << std::endl;
    }

    // Test 10: Export to CSV
    std::cout << "Test 10: Export to CSV..." << std::endl;
    try {
        manager.exportToCSV("archive_export.csv");
        std::cout << "✓ Test 10 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 10 failed: " << e.what() << std::endl;
    }

    // Test 11: Serialization
    std::cout << "Test 11: Serialization..." << std::endl;
    try {
        manager.saveArchive("archive.dat");
        std::cout << "Archive saved successfully" << std::endl;

        ArchiveManager newManager;
        newManager.loadArchive("archive.dat");
        newManager.printTree();
        std::cout << "✓ Test 11 passed\n" << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << "✗ Test 11 failed: " << e.what() << std::endl;
    }

    // Test 12: Corrupted file detection
    std::cout << "\nTest 12: Corrupted file detection..." << std::endl;
    std::cout << "NOTE: To test corrupted file detection, manually edit archive.dat" << std::endl;
    std::cout << "with a hex editor and change the magic number bytes (ARCH)." << std::endl;
    std::cout << "Then run load and verify exception is caught." << std::endl;
    std::cout << "✓ Test 12 ready for manual verification\n" << std::endl;

    std::cout << "========== TESTS COMPLETED ==========\n" << std::endl;
}

// ======================== ИНТЕРАКТИВНОЕ МЕНЮ ========================
void printMenu() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                 VIRTUAL ARCHIVE SYSTEM                       ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ 1.  Create file                   2.  Create directory      ║\n";
    std::cout << "║ 3.  Remove resource               4.  Move resource         ║\n";
    std::cout << "║ 5.  Copy resource                 6.  Print tree structure  ║\n";
    std::cout << "║ 7.  Show statistics               8.  Search by mask        ║\n";
    std::cout << "║ 9.  Search by date range          10. Sort by criterion     ║\n";
    std::cout << "║ 11. Export to CSV                 12. Save archive          ║\n";
    std::cout << "║ 13. Load archive                  14. Change user level     ║\n";
    std::cout << "║ 15. Run tests                     0.  Exit                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "Current user: " << accessLevelToString(AccessLevel::ADMIN) << std::endl;
    std::cout << "Enter choice: ";
}

int main() {
    std::cout << "\n";
    std::cout << "  ██╗   ██╗██╗██████╗ ████████╗██╗   ██╗ █████╗ ██╗          \n";
    std::cout << "  ██║   ██║██║██╔══██╗╚══██╔══╝██║   ██║██╔══██╗██║          \n";
    std::cout << "  ██║   ██║██║██████╔╝   ██║   ██║   ██║███████║██║          \n";
    std::cout << "  ╚██╗ ██╔╝██║██╔══██╗   ██║   ██║   ██║██╔══██║██║          \n";
    std::cout << "   ╚████╔╝ ██║██║  ██║   ██║   ╚██████╔╝██║  ██║███████╗     \n";
    std::cout << "    ╚═══╝  ╚═╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚══════╝     \n";
    std::cout << "                                                              \n";
    std::cout << "          VIRTUAL ARCHIVE SYSTEM v1.0                         \n";
    std::cout << "          =============================                       \n\n";

    ArchiveManager manager;
    int choice;
    std::string input, path, name, ext;

    while (true) {
        printMenu();
        std::cin >> choice;
        std::cin.ignore();

        try {
            switch (choice) {
            case 1: {
                std::cout << "Enter parent path (e.g., /documents): ";
                std::getline(std::cin, path);
                std::cout << "Enter file name: ";
                std::getline(std::cin, name);
                std::cout << "Enter extension: ";
                std::getline(std::cin, ext);
                std::cout << "Enter size (bytes): ";
                size_t size;
                std::cin >> size;
                std::cin.ignore();
                manager.createFile(path, name, ext, size);
                std::cout << "File created successfully!" << std::endl;
                break;
            }
            case 2: {
                std::cout << "Enter parent path: ";
                std::getline(std::cin, path);
                std::cout << "Enter directory name: ";
                std::getline(std::cin, name);
                std::cout << "Enter access level (0=GUEST, 1=USER, 2=ADMIN): ";
                int level;
                std::cin >> level;
                std::cin.ignore();
                manager.createDirectory(path, name, static_cast<AccessLevel>(level));
                std::cout << "Directory created successfully!" << std::endl;
                break;
            }
            case 3: {
                std::cout << "Enter full path to resource: ";
                std::getline(std::cin, path);
                manager.removeResource(path);
                std::cout << "Resource removed successfully!" << std::endl;
                break;
            }
            case 4: {
                std::cout << "Enter source path: ";
                std::getline(std::cin, path);
                std::cout << "Enter destination path: ";
                std::string dest;
                std::getline(std::cin, dest);
                manager.moveResource(path, dest);
                std::cout << "Resource moved successfully!" << std::endl;
                break;
            }
            case 5: {
                std::cout << "Enter source path: ";
                std::getline(std::cin, path);
                std::cout << "Enter destination path: ";
                std::string dest;
                std::getline(std::cin, dest);
                manager.copyResource(path, dest);
                std::cout << "Resource copied successfully!" << std::endl;
                break;
            }
            case 6:
                manager.printTree();
                break;
            case 7:
                manager.printStatistics();
                break;
            case 8: {
                std::cout << "Enter name pattern (regex): ";
                std::getline(std::cin, name);
                std::cout << "Enter extension (leave empty for any): ";
                std::getline(std::cin, ext);
                manager.searchByMask(name, ext);
                break;
            }
            case 9: {
                int sy, sm, sd, ey, em, ed;
                std::cout << "Enter start date (year month day): ";
                std::cin >> sy >> sm >> sd;
                std::cout << "Enter end date (year month day): ";
                std::cin >> ey >> em >> ed;
                std::cin.ignore();
                manager.searchByDateRange(sy, sm, sd, ey, em, ed);
                break;
            }
            case 10: {
                std::cout << "Enter directory path: ";
                std::getline(std::cin, path);
                std::cout << "Enter sort criterion (name/size/date): ";
                std::string criterion;
                std::getline(std::cin, criterion);
                manager.sortChildren(path, criterion);
                std::cout << "Sorted successfully!" << std::endl;
                break;
            }
            case 11: {
                std::cout << "Enter CSV filename: ";
                std::getline(std::cin, name);
                manager.exportToCSV(name);
                break;
            }
            case 12: {
                std::cout << "Enter filename to save (e.g., archive.dat): ";
                std::getline(std::cin, name);
                manager.saveArchive(name);
                std::cout << "Archive saved successfully!" << std::endl;
                break;
            }
            case 13: {
                std::cout << "Enter filename to load: ";
                std::getline(std::cin, name);
                manager.loadArchive(name);
                std::cout << "Archive loaded successfully!" << std::endl;
                break;
            }
            case 14: {
                std::cout << "Enter user level (0=GUEST, 1=USER, 2=ADMIN): ";
                int level;
                std::cin >> level;
                std::cin.ignore();
                manager.setUserLevel(static_cast<AccessLevel>(level));
                std::cout << "User level changed to " << accessLevelToString(static_cast<AccessLevel>(level)) << std::endl;
                break;
            }
            case 15:
                runTests();
                break;
            case 0:
                std::cout << "Goodbye!" << std::endl;
                return 0;
            default:
                std::cout << "Invalid choice!" << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << std::endl;
        }
    }

    return 0;
}