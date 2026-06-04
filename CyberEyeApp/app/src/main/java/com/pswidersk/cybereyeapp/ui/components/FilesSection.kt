package com.pswidersk.cybereyeapp.ui.components

import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.ShoppingCart
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pswidersk.cybereyeapp.client.FilesClient
import com.pswidersk.cybereyeapp.client.RemoteFile
import kotlinx.coroutines.launch

@Composable
fun FilesSection() {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()

    val files = remember { mutableStateListOf<RemoteFile>() }
    var isLoading by remember { mutableStateOf(false) }
    var busyFile by remember { mutableStateOf<String?>(null) }

    fun loadFiles() {
        coroutineScope.launch {
            isLoading = true
            FilesClient.listFiles()
                .onSuccess { result ->
                    files.clear()
                    files.addAll(result.sortedWith(compareBy({ !it.dir }, { it.name })))
                }
                .onFailure {
                    Toast.makeText(
                        context,
                        "Failed to list files: ${it.message}",
                        Toast.LENGTH_SHORT
                    ).show()
                }
            isLoading = false
        }
    }

    fun deleteFile(file: RemoteFile) {
        coroutineScope.launch {
            busyFile = file.name
            FilesClient.deleteFile(file.name)
                .onSuccess {
                    files.remove(file)
                    Toast.makeText(context, "Deleted ${file.name}", Toast.LENGTH_SHORT).show()
                }
                .onFailure {
                    Toast.makeText(context, "Delete failed: ${it.message}", Toast.LENGTH_SHORT)
                        .show()
                }
            busyFile = null
        }
    }

    fun downloadFile(file: RemoteFile) {
        coroutineScope.launch {
            busyFile = file.name
            FilesClient.downloadFile(file.name)
                .onSuccess { bytes ->
                    FilesClient.saveToDownloads(context, file.name, bytes)
                        .onSuccess {
                            Toast.makeText(
                                context,
                                "Saved ${file.name} to Downloads",
                                Toast.LENGTH_SHORT
                            ).show()
                        }
                        .onFailure {
                            Toast.makeText(
                                context,
                                "Save failed: ${it.message}",
                                Toast.LENGTH_SHORT
                            ).show()
                        }
                }
                .onFailure {
                    Toast.makeText(context, "Download failed: ${it.message}", Toast.LENGTH_SHORT)
                        .show()
                }
            busyFile = null
        }
    }

    val uploadLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri ->
        uri ?: return@rememberLauncherForActivityResult
        val filename = context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            cursor.moveToFirst()
            cursor.getString(nameIndex)
        } ?: uri.lastPathSegment?.substringAfterLast('/') ?: "upload"
        coroutineScope.launch {
            isLoading = true
            FilesClient.uploadFile(context, filename, uri)
                .onSuccess {
                    Toast.makeText(context, "Uploaded $filename", Toast.LENGTH_SHORT).show()
                    loadFiles()
                }
                .onFailure {
                    Toast.makeText(context, "Upload failed: ${it.message}", Toast.LENGTH_SHORT)
                        .show()
                }
            isLoading = false
        }
    }

    LaunchedEffect(Unit) { loadFiles() }

    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            SectionTitle("Files")
        }

        Spacer(Modifier.height(8.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            IconButton(onClick = { uploadLauncher.launch("*/*") }) {
                Icon(Icons.Default.Add, contentDescription = "Upload", tint = Color.White)
            }
            IconButton(onClick = { loadFiles() }) {
                Icon(Icons.Default.Refresh, contentDescription = "Refresh", tint = Color.White)
            }
        }

        Spacer(Modifier.height(8.dp))

        AnimatedVisibility(visible = isLoading) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.Center
            ) {
                CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp)
            }
        }

        AnimatedVisibility(visible = !isLoading && files.isEmpty()) {
            Text("No files found", fontSize = 12.sp, color = Color.White.copy(alpha = 0.6f))
        }

        LazyColumn(
            modifier = Modifier
                .fillMaxWidth()
                .height(220.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            items(files, key = { it.name }) { file ->
                FileRow(
                    file = file,
                    isBusy = busyFile == file.name,
                    onDelete = { deleteFile(file) },
                    onDownload = { downloadFile(file) }
                )
                HorizontalDivider(color = Color.White.copy(alpha = 0.1f))
            }
        }
    }
}

@Composable
private fun FileRow(
    file: RemoteFile,
    isBusy: Boolean,
    onDelete: () -> Unit,
    onDownload: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = if (file.dir) "📁 ${file.name}" else file.name,
                fontSize = 12.sp,
                color = Color.White,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            if (!file.dir) {
                Text(
                    text = formatSize(file.size),
                    fontSize = 10.sp,
                    color = Color.White.copy(alpha = 0.5f)
                )
            }
        }

        Spacer(Modifier.width(8.dp))

        if (isBusy) {
            CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
        } else {
            Row {
                if (!file.dir) {
                    FilledTonalIconButton(onClick = onDownload, modifier = Modifier.size(32.dp)) {
                        Icon(
                            Icons.Default.ShoppingCart,
                            contentDescription = "Download",
                            modifier = Modifier.size(16.dp)
                        )
                    }
                    Spacer(Modifier.width(4.dp))
                }
                FilledTonalIconButton(onClick = onDelete, modifier = Modifier.size(32.dp)) {
                    Icon(
                        Icons.Default.Delete,
                        contentDescription = "Delete",
                        modifier = Modifier.size(16.dp)
                    )
                }
            }
        }
    }
}

private fun formatSize(bytes: Long): String = when {
    bytes >= 1_048_576 -> "%.1f MB".format(bytes / 1_048_576.0)
    bytes >= 1_024 -> "%.1f KB".format(bytes / 1_024.0)
    else -> "$bytes B"
}