package com.hani.btapp.core.service

/**
 * Created by hanif on 2022-08-22.
 */
interface GattInteractor {
    fun writeData(data: ByteArray) : Boolean
    suspend fun connectIfNeeded()
    suspend fun enableReconnect()
    suspend fun disableReconnect()
    suspend fun waitNewConnection() : Boolean
    suspend fun makeNewConnection() : Boolean
    suspend fun disconnectDevice()
    suspend fun clearConnnectionStatus()
    suspend fun IsConnected() : Boolean
}