// https://github.com/carranzarodrigo378-ops/TpIntegrador_BotTelegramC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

struct memory {
  char *response;
  size_t size;
};

static size_t cb(char *data, size_t size, size_t nmemb, void *clientp)
{
  size_t realsize = size * nmemb;
  struct memory *mem = clientp;

  char *ptr = realloc(mem->response, mem->size + realsize + 1);
  if (!ptr)
    return 0; /* out of memory */

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;

  return realsize;
}

int leer_token(const char *archivo, char *token, size_t size)
{
  FILE *file = fopen(archivo, "r");
  if (file == NULL) {
    printf("Error al abrir el archivo %s\n", archivo);
    return 1;
  }
  if (fgets(token, size, file) == NULL) {
    printf("Error al leer el token del archivo %s\n", archivo);
    fclose(file);
    return 1;
  }

  // Eliminar saltos de línea (\n y \r) al final del token
  size_t len = strlen(token);
  while (len > 0 && (token[len - 1] == '\n' || token[len - 1] == '\r')) {
    token[len - 1] = '\0';
    len--;
  }

  fclose(file);
  return 0;
}

char *hacer_peticiones(const char *url)
{
  CURLcode res;
  CURL *curl = curl_easy_init();
  struct memory chunk = {0};

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    res = curl_easy_perform(curl);
    if (res != 0) {
      printf("Error Código: %d\n", res);
      free(chunk.response);
      curl_easy_cleanup(curl);
      return NULL;
    }
    curl_easy_cleanup(curl);
    return chunk.response;
  } else {
    printf("Error al inicializar cURL\n");
    return NULL;
  }
}

long long extraer_numero(const char *json, const char *clave)
{
  char *resultado = strstr(json, clave);
  if (resultado) {
    resultado += strlen(clave); // Mueve el puntero al final de la clave
    return strtoll(resultado, NULL, 10); // Convierte el texto a long long
  } else {
    return -1;
  }
}

int extraer_texto(const char *json, const char *clave, char *destino, size_t max_len)
{
  char *inicio = strstr(json, clave);
  if (inicio) {
    inicio += strlen(clave);
    char *fin = strchr(inicio, '"'); // Busca la comilla de cierre
    if (fin) {
      size_t len = fin - inicio;
      if (len >= max_len) {
        len = max_len - 1;
      }
      strncpy(destino, inicio, len);
      destino[len] = '\0';
      return 0;
    } else {
      return -1;
    }
  } else {
    return -1;
  }
}

// Reemplaza espacios por "%20" para que la URL de sendMessage no se corte
void codificar_url(const char *origen, char *destino, size_t max_len)
{
  size_t j = 0;
  for (size_t i = 0; origen[i] != '\0' && j + 3 < max_len; i++) {
    if (origen[i] == ' ') {
      destino[j++] = '%';
      destino[j++] = '2';
      destino[j++] = '0';
    } else {
      destino[j++] = origen[i];
    }
  }
  destino[j] = '\0';
}

// Construye la URL de sendMessage, envía la respuesta y libera la memoria
void enviar_mensaje(const char *token, long long chat_id, const char *texto)
{
  char texto_codificado[1024];
  codificar_url(texto, texto_codificado, sizeof(texto_codificado));

  char url[1536];
  snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage?chat_id=%lld&text=%s",
           token, chat_id, texto_codificado);

  char *resp = hacer_peticiones(url);
  if (resp) {
    free(resp); // No necesitamos leer la confirmación, solo liberar memoria
  }
}

int main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("El programa requiere pasar el token como parametro.\n");
    return 1;
  }

  char token[256];
  if (leer_token(argv[1], token, sizeof(token)) != 0) {
    return 1;
  }

  // Guarda el ID del próximo mensaje a leer para no repetir mensajes viejos
  long long offset = 0;
  char url[512];

  printf("Bot iniciado y escuchando mensajes...\n");

  // Bucle infinito que consulta Telegram cada 2 segundos
  while (1) {
    // Si ya leímos algún mensaje, pedimos solo los posteriores a 'offset'
    if (offset > 0) {
      snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/getUpdates?offset=%lld", token, offset);
    } else {
      snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/getUpdates", token);
    }

    char *response = hacer_peticiones(url);

    if (response) {
      printf("JSON: %s\n", response);
      long long update_id = extraer_numero(response, "\"update_id\":");

      // Si update_id != -1 significa que llegó un mensaje nuevo
      if (update_id != -1) {
        long long chat_id = extraer_numero(response, "\"chat\":{\"id\":");
        long long date = extraer_numero(response, "\"date\":");

        // Inicializamos en vacío por si llega una foto/sticker sin texto
        char first_name[256] = "Usuario";
        char text[256] = "";

        extraer_texto(response, "\"first_name\":\"", first_name, sizeof(first_name));
        extraer_texto(response, "\"text\":\"", text, sizeof(text));

        printf("\n--- Nuevo mensaje ---\n");
        printf("Update ID: %lld\n", update_id);
        printf("Chat ID: %lld\n", chat_id);
        printf("First Name: %s\n", first_name);
        printf("Date: %lld\n", date);
        printf("Text: %s\n", text);

        // Actualizamos el offset para marcar este mensaje como leído en la próxima vuelta
        offset = update_id + 1;

        // Lógica de respuestas del bot según lo recibido
        if (chat_id != -1 && strlen(text) > 0) {
          char respuesta_bot[512];

          if (strcmp(text, "hola") == 0 || strcmp(text, "Hola") == 0) {
            snprintf(respuesta_bot, sizeof(respuesta_bot), "Hola %s", first_name);
          } else if (strcmp(text, "chau") == 0 || strcmp(text, "Chau") == 0) {
            snprintf(respuesta_bot, sizeof(respuesta_bot), "Chau %s", first_name);
          } else {
            // Si escribe cualquier otra cosa, repite el mensaje
            snprintf(respuesta_bot, sizeof(respuesta_bot), "%s", text);
          }

          enviar_mensaje(token, chat_id, respuesta_bot);
        }
      }

      free(response); // Liberamos la memoria de getUpdates en cada vuelta
    }

    // Espera de 2 segundos antes de volver a consultar
    sleep(2);
  }

  return 0;
}