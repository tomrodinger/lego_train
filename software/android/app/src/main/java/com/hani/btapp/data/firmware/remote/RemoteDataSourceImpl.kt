package com.hani.btapp.data.firmware.remote

import android.content.Context
import com.hani.btapp.Logger
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
import kotlinx.serialization.decodeFromString
import java.nio.ByteBuffer
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

    override suspend fun fetchFirmwareData(DevName: String, firmwareFileName: String): Result<ByteArray> {
        val client = getHttpClient()

        var host_path = ""

        if (DevName.contains("lego_train_")) {
            host_path = "raw.githubusercontent.com/tomrodinger/lego_train/master/firmware_releases/"
        } else if (DevName.contains("robot_bl702")) {
            host_path = "raw.githubusercontent.com/tomrodinger/line_following_robot/master/firmware_releases/"
        }

        val response = client.use {
            it.get{
                url {
                protocol = URLProtocol.HTTP
                host = host_path
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

    override suspend fun fetchAvailableFirmwares(DevName: String): Result<Product> {
        val client = getHttpClient()
        var host_path = ""

        if (DevName.contains("lego_train_")) {
            host_path = "raw.githubusercontent.com/tomrodinger/lego_train/master/firmware_releases/"
        } else if (DevName.contains("robot_bl702")) {
            host_path = "raw.githubusercontent.com/tomrodinger/line_following_robot/master/firmware_releases/"
        }

        val response = client.use {
            it.get{
                contentType(ContentType.Application.Json)
                url {
                    protocol = URLProtocol.HTTP
                    host = host_path
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
                val str_res: String = response.body()
                val json = kotlinx.serialization.json.Json {
                    ignoreUnknownKeys = true
                }
                var product_res: ProductResponse = json.decodeFromString(str_res)

                Result.success(product_res.product)
            }
            else -> {
                Result.failure(Exception("Could not fetch available firmwares. status: $status"))
            }
        }
    }


}