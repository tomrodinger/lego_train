package com.hani.btapp.data.firmware.remote

import com.hani.btapp.domain.Product

/**
 * Created by hanif on 2022-08-09.
 */
interface RemoteDataSource {
    suspend fun fetchFirmwareData(DevName: String, firmwareFileName: String): Result<ByteArray>
    suspend fun fetchAvailableFirmwares(DevName: String): Result<Product>
}