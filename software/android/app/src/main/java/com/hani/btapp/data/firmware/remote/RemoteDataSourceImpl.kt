package com.hani.btapp.data.firmware.remote

import android.content.Context
import com.hani.btapp.data.NetworkClient
import com.hani.btapp.domain.Product
import com.hani.btapp.domain.ProductResponse
import dagger.hilt.android.qualifiers.ApplicationContext
import io.ktor.client.*
import io.ktor.client.call.*
import io.ktor.client.plugins.*
import io.ktor.client.plugins.contentnegotiation.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import io.ktor.http.*
import io.ktor.serialization.kotlinx.json.*
import kotlinx.serialization.json.Json
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Created by hanif on 2022-08-09.
 */
@Singleton
class RemoteDataSourceImpl @Inject constructor(
    // Add a network client e.g. ktor

    @ApplicationContext private val context: Context
) : RemoteDataSource {

    private fun getHttpClient(): HttpClient {
        return HttpClient {
            install(ContentNegotiation) {
                json(Json {
                    prettyPrint = false
                    isLenient = false
                    ignoreUnknownKeys = true
                    explicitNulls = false
                })
            }
            install(HttpRequestRetry) {
                retryOnServerErrors(maxRetries = 3)
                exponentialDelay()
            }
            defaultRequest {
                accept(ContentType.parse("application/json"))
                accept(ContentType.parse("text/html"))
            }
        }
    }

    override suspend fun fetchFirmwareData(firmwareFileName: String): Result<ByteArray> {
        val client = getHttpClient()
        val response = client.use {
            it.get{
                url {
                protocol = URLProtocol.HTTP
                host = "9o.at"
                path(firmwareFileName)
            }}
        }
       /* val response = client.get {
            url {
                protocol = URLProtocol.HTTP
                host = "9o.at"
                path(firmwareFileName)
            }
        }*/
        return when (val status = response.status) {
            HttpStatusCode.OK -> {
                Result.success(response.readBytes())
            }
            else -> {
                Result.failure(Exception("Could not fetch firmware file: $firmwareFileName." +
                        " status: $status"))
            }
        }
    }

    override suspend fun fetchAvailableFirmwares(): Result<Product> {
        val client = getHttpClient()
        val response = client.use {
            it.get{
                contentType(ContentType.Application.Json)
                url {
                    protocol = URLProtocol.HTTP
                    host = "9o.at"
                    path("firmware_list.json")
                }}
        }
        /*val response = client.get {
            contentType(ContentType.Application.Json)
            url {
                protocol = URLProtocol.HTTP
                host = "9o.at"
                path("firmware_list.json")
            }
        }*/
        return when (val status = response.status) {
            HttpStatusCode.OK -> {
                val res: ProductResponse = response.body()
                Result.success(res.product)
            }
            else -> {
                Result.failure(Exception("Could not fetch available firmwares. status: $status"))
            }
        }
    }


}