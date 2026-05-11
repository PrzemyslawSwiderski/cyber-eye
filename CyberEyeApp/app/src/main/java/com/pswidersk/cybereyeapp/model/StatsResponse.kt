package com.pswidersk.cybereyeapp.model

import kotlinx.serialization.Serializable

@Serializable
data class StatsResponse(
    val status: Status? = null,
    val error: String? = null
)