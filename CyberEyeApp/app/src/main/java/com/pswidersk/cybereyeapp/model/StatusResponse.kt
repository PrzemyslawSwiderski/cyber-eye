package com.pswidersk.cybereyeapp.model

import kotlinx.serialization.Serializable

@Serializable
data class StatusResponse(
    val status: Status = Status.UNKNOWN,
    val error: String? = null
)