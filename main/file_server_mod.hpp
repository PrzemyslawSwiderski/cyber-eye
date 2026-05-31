#pragma once

#include <string>
#include <vector>
#include <functional>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "esp_http_server.h"
#include "esp_log.h"

class HttpFileServer
{
public:
    struct FileInfo
    {
        std::string name;
        size_t size;
        bool is_dir;
    };

    explicit HttpFileServer(const std::string &base_path = "/sdcard")
        : base_path_(base_path), server_(nullptr) {}

    ~HttpFileServer()
    {
        stop();
    }

    esp_err_t start(uint16_t port = 80)
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = port;
        config.max_uri_handlers = 8;
        config.uri_match_fn = httpd_uri_match_wildcard;

        if (httpd_start(&server_, &config) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start HTTP server");
            return ESP_FAIL;
        }

        registerHandlers();
        ESP_LOGI(TAG, "HTTP file server started on port %d", port);
        return ESP_OK;
    }

    void stop()
    {
        if (server_)
        {
            httpd_stop(server_);
            server_ = nullptr;
            ESP_LOGI(TAG, "HTTP file server stopped");
        }
    }

private:
    static constexpr const char *TAG = "HTTP_FILE";
    std::string base_path_;
    httpd_handle_t server_;

    void registerHandlers()
    {
        // List files (GET /list)
        httpd_uri_t list_uri = {
            .uri = "/api/files/list",
            .method = HTTP_GET,
            .handler = listHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &list_uri);

        // Download file (GET /download?file=filename)
        httpd_uri_t download_uri = {
            .uri = "/api/files/download",
            .method = HTTP_GET,
            .handler = downloadHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &download_uri);

        // Upload file (POST /upload?file=filename)
        httpd_uri_t upload_uri = {
            .uri = "/api/files/upload",
            .method = HTTP_POST,
            .handler = uploadHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &upload_uri);

        // Delete file (DELETE /delete?file=filename)
        httpd_uri_t delete_uri = {
            .uri = "/api/files/delete",
            .method = HTTP_DELETE,
            .handler = deleteHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &delete_uri);
    }

    static esp_err_t listHandler(httpd_req_t *req)
    {
        auto *self = static_cast<HttpFileServer *>(req->user_ctx);
        std::string json = "[";
        
        DIR *dir = opendir(self->base_path_.c_str());
        if (!dir)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        bool first = true;
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            std::string full_path = self->base_path_ + "/" + entry->d_name;
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0)
            {
                if (!first) json += ",";
                first = false;
                
                json += "{\"name\":\"";
                json += entry->d_name;
                json += "\",\"size\":";
                json += std::to_string(st.st_size);
                json += ",\"dir\":";
                json += S_ISDIR(st.st_mode) ? "true" : "false";
                json += "}";
            }
        }
        closedir(dir);
        
        json += "]";
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json.c_str(), json.length());
        return ESP_OK;
    }

    static esp_err_t downloadHandler(httpd_req_t *req)
    {
        auto *self = static_cast<HttpFileServer *>(req->user_ctx);
        
        char filename[256] = {0};
        if (httpd_req_get_url_query_str(req, filename, sizeof(filename)) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
            return ESP_FAIL;
        }

        char value[128] = {0};
        if (httpd_query_key_value(filename, "file", value, sizeof(value)) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
            return ESP_FAIL;
        }

        std::string filepath = self->base_path_ + "/" + value;
        
        // Check if file exists
        struct stat st;
        if (stat(filepath.c_str(), &st) != 0 || S_ISDIR(st.st_mode))
        {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }

        // Open and send file
        FILE *f = fopen(filepath.c_str(), "rb");
        if (!f)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, "application/octet-stream");
        
        char buf[1024];
        size_t read;
        while ((read = fread(buf, 1, sizeof(buf), f)) > 0)
        {
            httpd_resp_send_chunk(req, buf, read);
        }
        httpd_resp_send_chunk(req, nullptr, 0);
        
        fclose(f);
        return ESP_OK;
    }

    static esp_err_t uploadHandler(httpd_req_t *req)
    {
        auto *self = static_cast<HttpFileServer *>(req->user_ctx);
        
        char filename[256] = {0};
        if (httpd_req_get_url_query_str(req, filename, sizeof(filename)) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get filename");
            return ESP_FAIL;
        }

        char value[128] = {0};
        if (httpd_query_key_value(filename, "file", value, sizeof(value)) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get filename");
            return ESP_FAIL;
        }

        // Validate filename (prevent directory traversal)
        if (strchr(value, '/') || strchr(value, '\\') || strstr(value, ".."))
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
            return ESP_FAIL;
        }

        std::string filepath = self->base_path_ + "/" + value;
        
        FILE *f = fopen(filepath.c_str(), "wb");
        if (!f)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
            return ESP_FAIL;
        }

        size_t total = 0;
        int remaining = req->content_len;
        char buf[512];

        while (remaining > 0)
        {
            int received = httpd_req_recv(req, buf, std::min(remaining, (int)sizeof(buf)));
            if (received <= 0)
            {
                fclose(f);
                unlink(filepath.c_str());  // Delete partial file
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to upload file");
                return ESP_FAIL;
            }
            
            fwrite(buf, 1, received, f);
            remaining -= received;
            total += received;
        }
        
        fclose(f);
        
        char response[128];
        snprintf(response, sizeof(response), "{\"status\":\"ok\",\"file\":\"%s\",\"size\":%zu}", value, total);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        
        ESP_LOGI(TAG, "Uploaded: %s (%zu bytes)", value, total);
        return ESP_OK;
    }

    static esp_err_t deleteHandler(httpd_req_t *req)
    {
        auto *self = static_cast<HttpFileServer *>(req->user_ctx);
        
        char filename[256] = {0};
        if (httpd_req_get_url_query_str(req, filename, sizeof(filename)) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
            return ESP_FAIL;
        }

        char value[128] = {0};
        if (httpd_query_key_value(filename, "file", value, sizeof(value)) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
            return ESP_FAIL;
        }

        // Validate filename
        if (strchr(value, '/') || strchr(value, '\\') || strstr(value, ".."))
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
            return ESP_FAIL;
        }

        std::string filepath = self->base_path_ + "/" + value;
        
        if (unlink(filepath.c_str()) != 0)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", 13);
        
        ESP_LOGI(TAG, "Deleted: %s", value);
        return ESP_OK;
    }
};
