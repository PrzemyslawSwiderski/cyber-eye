package com.pswidersk.cybereyeapp.model

import kotlinx.serialization.Serializable

@Serializable
data class StatusResponse(
    val status: CameraStatus = CameraStatus.UNKNOWN,
    val error: String? = null
)