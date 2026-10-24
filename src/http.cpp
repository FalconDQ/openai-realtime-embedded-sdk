#include <esp_http_client.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "main.h"
#include "local_secrets.h"

struct HttpResponse {
  char *data;
  size_t length;
  bool truncated;
  char request_id[128];
};

esp_err_t oai_http_event_handler(esp_http_client_event_t *evt) {
  auto *response = static_cast<HttpResponse *>(evt->user_data);
  // Only retain the request identifier, never arbitrary headers or bodies.
  if (evt->event_id == HTTP_EVENT_ON_HEADER && response && evt->header_key &&
      evt->header_value && strcasecmp(evt->header_key, "x-request-id") == 0) {
    const size_t length = strlen(evt->header_value);
    bool safe = length > 0 && length < sizeof(response->request_id);
    for (size_t i = 0; safe && i < length; ++i) {
      const char c = evt->header_value[i];
      safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-';
    }
    if (safe) memcpy(response->request_id, evt->header_value, length + 1);
  }
  // esp_http_client already removes HTTP chunk framing before ON_DATA.
  if (evt->event_id == HTTP_EVENT_ON_DATA && response && evt->data_len > 0) {
    const size_t remaining = MAX_HTTP_OUTPUT_BUFFER - 1 - response->length;
    const size_t incoming = static_cast<size_t>(evt->data_len);
    const size_t count = incoming < remaining ? incoming : remaining;
    memcpy(response->data + response->length, evt->data, count);
    response->length += count;
    response->data[response->length] = '\0';
    response->truncated |= count < incoming;
  }
  return ESP_OK;
}

bool oai_http_request(char *offer, char *answer) {
  answer[0] = '\0';
  const char *prefix =
      "--esp32-realtime-call\r\n"
      "Content-Disposition: form-data; name=\"sdp\"\r\n"
      "Content-Type: application/sdp\r\n\r\n";
  const char *suffix =
      "\r\n--esp32-realtime-call\r\n"
      "Content-Disposition: form-data; name=\"session\"\r\n"
      "Content-Type: application/json\r\n\r\n"
      "{\"type\":\"realtime\",\"model\":\"gpt-realtime-mini\","
      "\"tracing\":{\"workflow_name\":\"DFR1154 Realtime debug\"}}"
      "\r\n--esp32-realtime-call--\r\n";
  const size_t body_size = strlen(prefix) + strlen(offer) + strlen(suffix) + 1;
  char *body = static_cast<char *>(malloc(body_size));
  if (!body) {
    ESP_LOGE(LOG_TAG, "Not enough memory for Realtime request");
    return false;
  }
  snprintf(body, body_size, "%s%s%s", prefix, offer, suffix);

  HttpResponse response = {answer, 0, false, {0}};
  esp_http_client_config_t config = {};
  config.url = OPENAI_REALTIMEAPI;
  config.event_handler = oai_http_event_handler;
  config.user_data = &response;
  config.timeout_ms = 20000;
  config.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(body);
    ESP_LOGE(LOG_TAG, "Could not initialize Realtime HTTP client");
    return false;
  }

  char authorization[sizeof(OPENAI_API_KEY) + 8];
  snprintf(authorization, sizeof(authorization), "Bearer %s", OPENAI_API_KEY);
  esp_err_t err = esp_http_client_set_method(client, HTTP_METHOD_POST);
  if (err == ESP_OK) err = esp_http_client_set_header(client, "Content-Type",
      "multipart/form-data; boundary=esp32-realtime-call");
  if (err == ESP_OK) err = esp_http_client_set_header(client, "Authorization", authorization);
  if (err == ESP_OK) err = esp_http_client_set_post_field(client, body, strlen(body));
  if (err == ESP_OK) err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  if (response.request_id[0])
    ESP_LOGI(LOG_TAG, "Realtime HTTP request_id=%s", response.request_id);
  const bool success = err == ESP_OK && status == 201 && !response.truncated &&
                       strncmp(answer, "v=0", 3) == 0;
  if (!success) {
    // Never print request/response bodies or authorization headers.
    ESP_LOGE(LOG_TAG, "Realtime HTTP request failed: transport=%s, HTTP status=%d (expected 201)",
             esp_err_to_name(err), status);
    if (response.truncated) ESP_LOGE(LOG_TAG, "Realtime response exceeded buffer capacity");
    if (err == ESP_OK && status == 201 && !response.truncated)
      ESP_LOGE(LOG_TAG, "Realtime response was not an SDP answer");
    answer[0] = '\0';
  } else {
    ESP_LOGI(LOG_TAG, "Realtime session accepted (HTTP 201)");
    ESP_LOGI(LOG_TAG, "Realtime tracing requested: DFR1154 Realtime debug (OpenAI Traces dashboard)");
  }
  esp_http_client_cleanup(client);
  memset(authorization, 0, sizeof(authorization));
  free(body);
  return success;
}
