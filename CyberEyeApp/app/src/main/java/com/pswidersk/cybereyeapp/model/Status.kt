package com.pswidersk.cybereyeapp.model

import kotlinx.serialization.SerialName

enum class Status {
    @SerialName("streaming")
    STREAMING,
    @SerialName("ok")
    OK,
    @SerialName("stopped")
    STOPPED
}
