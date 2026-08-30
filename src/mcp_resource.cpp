/**
 * @file mcp_resource.cpp
 * @brief Resource implementation for MCP
 * 
 * This file implements the resource classes for the Model Context Protocol.
 * Follows the 2024-11-05 protocol specification.
 */
#include "mcp_resource.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <mutex>

namespace fs = std::filesystem;

namespace mcp {

// text_resource implementation
text_resource::text_resource(std::string uri, 
                           const std::string& name, 
                           const std::string& mime_type,
                           const std::string& description)
    : uri_(uri), name_(name), mime_type_(mime_type), description_(description), modified_(false) {
}

json text_resource::get_metadata() const {
    return {
        {"uri", uri_},
        {"name", name_},
        {"mimeType", mime_type_},
        {"description", description_}
    };
}

json text_resource::read() const {
    modified_ = false;
    return {
        {"uri", uri_},
        {"mimeType", mime_type_},
        {"text", text_}
    };
}

bool text_resource::is_modified() const {
    return modified_;
}

std::string text_resource::get_uri() const {
    return uri_;
}

void text_resource::set_text(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        modified_ = true;
    }
}

std::string text_resource::get_text() const {
    return text_;
}

// binary_resource implementation
binary_resource::binary_resource(const std::string& uri, 
                               const std::string& name, 
                               const std::string& mime_type,
                               const std::string& description)
    : uri_(uri), name_(name), mime_type_(mime_type), description_(description), modified_(false) {
}

json binary_resource::get_metadata() const {
    return {
        {"uri", uri_},
        {"name", name_},
        {"mimeType", mime_type_},
        {"description", description_}
    };
}

json binary_resource::read() const {
    modified_ = false;
    
    // Base64 encode the binary data
    std::string base64_data;
    if (!data_.empty()) {
        base64_data = base64::encode(reinterpret_cast<const char*>(data_.data()), data_.size());
    }
    
    return {
        {"uri", uri_},
        {"mimeType", mime_type_},
        {"blob", base64_data}
    };
}

bool binary_resource::is_modified() const {
    return modified_;
}

std::string binary_resource::get_uri() const {
    return uri_;
}

void binary_resource::set_data(const uint8_t* data, size_t size) {
    data_.resize(size);
    if (size > 0) {
        std::memcpy(data_.data(), data, size);
    }
    modified_ = true;
}

const std::vector<uint8_t>& binary_resource::get_data() const {
    return data_;
}

// file_resource implementation
file_resource::file_resource(const std::string& file_path, 
                           const std::string& mime_type,
                           const std::string& description)
    : text_resource(make_file_uri(file_path), 
                   fs::path(file_path).filename().string(),
                   mime_type.empty() ? guess_mime_type(file_path) : mime_type,
                   description),
      file_path_(file_path),
      last_modified_(0) {
    
}

std::shared_ptr<file_resource> file_resource::create(
        const std::string& file_path,
        const std::string& mime_type,
        const std::string& description) {
    if (!fs::exists(file_path)) {
        std::string msg("File not found: ");
        msg += file_path;
        throw mcp_exception(error_code::invalid_params, msg);
    }
    return std::make_shared<file_resource>(file_path, mime_type, description);
}

json file_resource::read() const {
    // Read file content
    std::ifstream file(file_path_, std::ios::binary);
    if (!file) {
        std::string msg("Failed to open file: ");
        msg += file_path_;
        throw mcp_exception(error_code::internal_error, msg);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    // Update text content
    const_cast<file_resource*>(this)->set_text(buffer.str());
    
    // Update last modified time
    last_modified_ = fs::last_write_time(file_path_).time_since_epoch().count();
    
    // Mark as not modified after read
    modified_ = false;
    
    return {
        {"uri", uri_},
        {"mimeType", mime_type_},
        {"text", text_}
    };
}

bool file_resource::is_modified() const {
    if (!fs::exists(file_path_)) {
        return true; // File was deleted
    }
    
    time_t current_modified = fs::last_write_time(file_path_).time_since_epoch().count();
    return current_modified != last_modified_;
}

std::string file_resource::make_file_uri(const std::string& file_path) {
    std::string uri("file://");
    uri += file_path;
    return uri;
}

std::string file_resource::guess_mime_type(const std::string& file_path) {
    std::string ext = fs::path(file_path).extension().string();
    
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), 
                  [](unsigned char c) { return std::tolower(c); });
    
    // Common MIME types
    if (ext == ".txt") return "text/plain";
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "text/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".xml") return "application/xml";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".zip") return "application/zip";
    if (ext == ".doc" || ext == ".docx") return "application/msword";
    if (ext == ".xls" || ext == ".xlsx") return "application/vnd.ms-excel";
    if (ext == ".ppt" || ext == ".pptx") return "application/vnd.ms-powerpoint";
    if (ext == ".csv") return "text/csv";
    if (ext == ".md") return "text/markdown";
    if (ext == ".py") return "text/x-python";
    if (ext == ".cpp" || ext == ".cc") return "text/x-c++src";
    if (ext == ".h" || ext == ".hpp") return "text/x-c++hdr";
    if (ext == ".c") return "text/x-csrc";
    if (ext == ".rs") return "text/x-rust";
    if (ext == ".go") return "text/x-go";
    if (ext == ".java") return "text/x-java";
    if (ext == ".ts") return "text/x-typescript";
    if (ext == ".rb") return "text/x-ruby";
    
    // Default to binary if unknown
    return "application/octet-stream";
}

// resource_manager implementation
static std::mutex g_resource_manager_mutex;

resource_manager& resource_manager::instance() {
    static resource_manager instance;
    return instance;
}

void resource_manager::register_resource(std::shared_ptr<resource> resource) {
    if (!resource) {
        throw mcp_exception(error_code::invalid_params, "Cannot register null resource");
    }
    
    std::string uri = resource->get_uri();
    
    std::lock_guard<std::mutex> lock(g_resource_manager_mutex);
    resources_[uri] = resource;
}

bool resource_manager::unregister_resource(const std::string& uri) {
    std::lock_guard<std::mutex> lock(g_resource_manager_mutex);
    
    if (resources_.count(uri) == 0) {
        return false;
    }
    
    resources_.erase(uri);
    
    /* Remove any subscriptions for this resource. Written against `count`
       and `erase(key)` rather than an iterator: an iterator's type is not
       written anywhere, so `auto` has nothing to deduce it from. Collect
       the keys first, because erasing while walking invalidates the walk. */
    std::vector<int> doomed;
    for (const auto& [sub_id, sub] : subscriptions_) {
        if (sub.uri == uri) {
            doomed.push_back(sub_id);
        }
    }
    for (int sub_id : doomed) {
        subscriptions_.erase(sub_id);
    }
    
    return true;
}

std::shared_ptr<resource> resource_manager::get_resource(const std::string& uri) const {
    std::lock_guard<std::mutex> lock(g_resource_manager_mutex);
    
    if (resources_.count(uri) == 0) {
        return nullptr;
    }
    
    return resources_.at(uri);
}

json resource_manager::list_resources() const {
    std::lock_guard<std::mutex> lock(g_resource_manager_mutex);
    
    json resources = json::array();
    
    for (const auto& [uri, res] : resources_) {
        resources.push_back(res->get_metadata());
    }
    
    return {
        {"resources", resources}
    };
}

int resource_manager::subscribe(const std::string& uri,
                                resource_changed_fn callback,
                                void* context) {
    if (!callback) {
        throw mcp_exception(error_code::invalid_params, "Cannot subscribe with null callback");
    }
    
    std::lock_guard<std::mutex> lock(g_resource_manager_mutex);
    
    // Check if resource exists
    if (resources_.count(uri) == 0) {
        std::string msg("Resource not found: ");
        msg += uri;
        throw mcp_exception(error_code::invalid_params, msg);
    }
    
    int id = next_subscription_id_++;
    subscription sub;
    sub.uri = uri;
    sub.fn = callback;
    sub.context = context;
    subscriptions_[id] = sub;
    
    return id;
}

bool resource_manager::unsubscribe(int subscription_id) {
    std::lock_guard<std::mutex> lock(g_resource_manager_mutex);
    
    if (subscriptions_.count(subscription_id) == 0) {
        return false;
    }
    
    subscriptions_.erase(subscription_id);
    return true;
}

void resource_manager::notify_resource_changed(const std::string& uri) {
    std::lock_guard<std::mutex> lock(g_resource_manager_mutex);
    
    // Check if resource exists
    if (resources_.count(uri) == 0) {
        return;
    }
    
    // Notify all subscribers for this resource
    for (const auto& [id, sub] : subscriptions_) {
        if (sub.uri == uri) {
            /* A callback that throws is the caller's problem: this subset
               has no `catch`, and swallowing an error silently was never
               much of a policy anyway. */
            sub.fn(sub.context, uri);
        }
    }
}

} // namespace mcp