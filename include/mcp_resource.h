/**
 * @file mcp_resource.h
 * @brief Resource implementation for MCP
 * 
 * This file defines the base resource class and common resource types for the MCP protocol.
 * Follows the 2025-03-26 protocol specification.
 */

#ifndef MCP_RESOURCE_H
#define MCP_RESOURCE_H

#include "mcp_message.h"
#include "base64.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>

namespace mcp {

/**
 * @class resource
 * @brief Base class for MCP resources
 * 
 * The resource class defines the interface for resources that can be
 * accessed through the MCP protocol. Each resource is identified by a URI.
 */
class resource {
public:
    virtual ~resource() = default;
    
    /**
     * @brief Get resource metadata
     * @return Metadata as JSON
     */
    virtual json get_metadata() const = 0;
    
    /**
     * @brief Read the resource content
     * @return The resource content as JSON
     */
    virtual json read() const = 0;
    
    /**
     * @brief Check if the resource has been modified
     * @return True if the resource has been modified since last read
     */
    virtual bool is_modified() const = 0;
    
    /**
     * @brief Get the URI of the resource
     * @return The URI as string
     */
    virtual std::string get_uri() const = 0;
};

/**
 * @class text_resource
 * @brief Resource containing text content
 * 
 * The text_resource class provides a base implementation for resources
 * that contain text content.
 */
class text_resource : public resource {
public:
    /**
     * @brief Constructor
     * @param uri The URI of the resource
     * @param name The name of the resource
     * @param mime_type The MIME type of the resource
     * @param description Optional description of the resource
     */
    text_resource(const std::string& uri, 
                 const std::string& name, 
                 const std::string& mime_type,
                 const std::string& description = "");
    
    /**
     * @brief Get resource metadata
     * @return Metadata as JSON
     */
    json get_metadata() const override;
    
    /**
     * @brief Read the resource content
     * @return The resource content as JSON
     */
    json read() const override;
    
    /**
     * @brief Check if the resource has been modified
     * @return True if the resource has been modified since last read
     */
    bool is_modified() const override;
    
    /**
     * @brief Get the URI of the resource
     * @return The URI as string
     */
    std::string get_uri() const override;
    
    /**
     * @brief Set the text content of the resource
     * @param text The text content
     */
    void set_text(const std::string& text);
    
    /**
     * @brief Get the text content of the resource
     * @return The text content
     */
    std::string get_text() const;

protected:
    std::string uri_;
    std::string name_;
    std::string mime_type_;
    std::string description_;
    std::string text_;
    mutable bool modified_;
};

/**
 * @class binary_resource
 * @brief Resource containing binary content
 * 
 * The binary_resource class provides a base implementation for resources
 * that contain binary content.
 */
class binary_resource : public resource {
public:
    /**
     * @brief Constructor
     * @param uri The URI of the resource
     * @param name The name of the resource
     * @param mime_type The MIME type of the resource
     * @param description Optional description of the resource
     */
    binary_resource(const std::string& uri, 
                   const std::string& name, 
                   const std::string& mime_type,
                   const std::string& description = "");
    
    /**
     * @brief Get resource metadata
     * @return Metadata as JSON
     */
    json get_metadata() const override;
    
    /**
     * @brief Read the resource content
     * @return The resource content as JSON with base64-encoded data
     */
    json read() const override;
    
    /**
     * @brief Check if the resource has been modified
     * @return True if the resource has been modified since last read
     */
    bool is_modified() const override;
    
    /**
     * @brief Get the URI of the resource
     * @return The URI as string
     */
    std::string get_uri() const override;
    
    /**
     * @brief Set the binary content of the resource
     * @param data Pointer to the binary data
     * @param size Size of the binary data
     */
    void set_data(const uint8_t* data, size_t size);
    
    /**
     * @brief Get the binary content of the resource
     * @return The binary content
     */
    const std::vector<uint8_t>& get_data() const;

protected:
    std::string uri_;
    std::string name_;
    std::string mime_type_;
    std::string description_;
    std::vector<uint8_t> data_;
    mutable bool modified_;
};

/**
 * @class file_resource
 * @brief Resource for file system operations
 * 
 * The file_resource class provides access to files as resources.
 */
class file_resource : public text_resource {
public:
    /**
     * @brief Constructor
     * @param file_path The path to the file
     * @param mime_type The MIME type of the file (optional, will be guessed if not provided)
     * @param description Optional description of the resource
     */
    file_resource(const std::string& file_path, 
                 const std::string& mime_type = "",
                 const std::string& description = "");
    
    /**
     * @brief Read the resource content
     * @return The resource content as JSON
     */
    json read() const override;
    
    /**
     * @brief Check if the resource has been modified
     * @return True if the resource has been modified since last read
     */
    bool is_modified() const override;

private:
    std::string file_path_;
    mutable time_t last_modified_;
    
    /**
     * @brief Guess the MIME type from file extension
     * @param file_path The file path
     * @return The guessed MIME type
     */
    static std::string guess_mime_type(const std::string& file_path);
};

/**
 * @class resource_manager
 * @brief Manager for MCP resources
 * 
 * The resource_manager class provides a central registry for resources
 * and handles resource operations.
 */
class resource_manager {
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the singleton instance
     */
    static resource_manager& instance();
    
    /**
     * @brief Register a resource
     * @param resource Shared pointer to the resource
     */
    void register_resource(std::shared_ptr<resource> resource);
    
    /**
     * @brief Unregister a resource
     * @param uri The URI of the resource to unregister
     * @return True if the resource was unregistered
     */
    bool unregister_resource(const std::string& uri);
    
    /**
     * @brief Get a resource by URI
     * @param uri The URI of the resource
     * @return Shared pointer to the resource, or nullptr if not found
     */
    std::shared_ptr<resource> get_resource(const std::string& uri) const;
    
    /**
     * @brief List all registered resources
     * @return JSON array of resource metadata
     */
    json list_resources() const;
    
    /**
     * @brief Subscribe to resource changes
     * @param uri The URI of the resource to subscribe to
     * @param callback The callback function to call when the resource changes
     * @return Subscription ID
     */
    int subscribe(const std::string& uri, std::function<void(const std::string&)> callback);
    
    /**
     * @brief Unsubscribe from resource changes
     * @param subscription_id The subscription ID
     * @return True if the subscription was removed
     */
    bool unsubscribe(int subscription_id);
    
    /**
     * @brief Notify subscribers of resource changes
     * @param uri The URI of the resource that changed
     */
    void notify_resource_changed(const std::string& uri);

private:
    resource_manager() = default;
    ~resource_manager() = default;
    
    resource_manager(const resource_manager&) = delete;
    resource_manager& operator=(const resource_manager&) = delete;
    
    std::map<std::string, std::shared_ptr<resource>> resources_;
    std::map<int, std::pair<std::string, std::function<void(const std::string&)>>> subscriptions_;
    int next_subscription_id_ = 1;
};

} // namespace mcp

#endif // MCP_RESOURCE_H