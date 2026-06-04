package com.pswidersk.cybereyeapp.client

import android.content.Context
import android.net.Uri
import com.pswidersk.cybereyeapp.AppState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

@Serializable
data class RemoteFile(
    val name: String,
    val size: Long,
    val dir: Boolean
)

object FilesClient {

    private val json = Json { ignoreUnknownKeys = true }

    private fun baseUrl() = "http://${AppState.cameraIp.value}:8080"

    suspend fun listFiles(): Result<List<RemoteFile>> = withContext(Dispatchers.IO) {
        runCatching {
            val url = URL("${baseUrl()}/api/files/list")
            val connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "GET"
            connection.connectTimeout = 5000
            connection.readTimeout = 5000
            val body = connection.inputStream.bufferedReader().readText()
            connection.disconnect()
            json.decodeFromString<List<RemoteFile>>(body)
        }
    }

    suspend fun deleteFile(filename: String): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val url = URL("${baseUrl()}/api/files/delete?file=$filename")
            val connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "DELETE"
            connection.connectTimeout = 5000
            connection.readTimeout = 5000
            connection.responseCode // trigger request
            connection.disconnect()
        }
    }

    suspend fun downloadFile(filename: String): Result<ByteArray> = withContext(Dispatchers.IO) {
        runCatching {
            val url = URL("${baseUrl()}/api/files/download?file=$filename")
            val connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "GET"
            connection.connectTimeout = 5000
            connection.readTimeout = 30000
            val bytes = connection.inputStream.readBytes()
            connection.disconnect()
            bytes
        }
    }

    suspend fun uploadFile(
        context: Context,
        filename: String,
        uri: Uri
    ): Result<Unit> = withContext(Dispatchers.IO) {
        runCatching {
            val inputStream: InputStream = context.contentResolver.openInputStream(uri)
                ?: error("Cannot open file: $uri")
            val bytes = inputStream.readBytes()
            inputStream.close()

            val url = URL("${baseUrl()}/api/files/upload?file=$filename")
            val connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "POST"
            connection.doOutput = true
            connection.setRequestProperty("Content-Type", "application/octet-stream")
            connection.setRequestProperty("Content-Length", bytes.size.toString())
            connection.connectTimeout = 5000
            connection.readTimeout = 30000
            connection.outputStream.write(bytes)
            connection.outputStream.flush()
            connection.responseCode // trigger request
            connection.disconnect()
        }
    }

    suspend fun saveToDownloads(
        context: Context,
        filename: String,
        bytes: ByteArray
    ): Result<Int> = withContext(Dispatchers.IO) {
        runCatching {
            val contentValues = android.content.ContentValues().apply {
                put(android.provider.MediaStore.Downloads.DISPLAY_NAME, filename)
                put(android.provider.MediaStore.Downloads.MIME_TYPE, "application/octet-stream")
                put(android.provider.MediaStore.Downloads.IS_PENDING, 1)
            }
            val resolver = context.contentResolver
            val uri = resolver.insert(
                android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI,
                contentValues
            ) ?: error("Failed to create download entry")

            resolver.openOutputStream(uri)!!.use { it.write(bytes) }

            contentValues.clear()
            contentValues.put(android.provider.MediaStore.Downloads.IS_PENDING, 0)
            resolver.update(uri, contentValues, null, null)
        }
    }
}