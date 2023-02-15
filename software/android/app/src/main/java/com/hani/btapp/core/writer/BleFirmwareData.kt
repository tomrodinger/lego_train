package com.hani.btapp.core.writer

import com.hani.btapp.Logger

/**
 * Created by hanif on 2022-08-08.
 */

class BleFirmwareData(private val firmwareData: ByteArray) {

    val totalBytes = firmwareData.size

    var bytesRead = 0
       private set

    private var nextPageIndex = 0
    private var program_size = 4096

    var isReady = false
       private set

    fun reset() {
        nextPageIndex = 0
        bytesRead = 0
        isReady = false
    }

    fun getNextChunk(): ByteArray? {
        if (nextPageIndex >= totalBytes) {
            return null
        }
        var startIndex = nextPageIndex
        var endIndex = nextPageIndex + program_size
        if (endIndex > totalBytes) {
            endIndex = totalBytes
        }
        return try {
            val nextChunk = firmwareData.copyOfRange(startIndex, endIndex)
            bytesRead += nextChunk.size
            Logger.log("getNextChunk: [$startIndex, $endIndex], " +
                    "totalSize: $totalBytes, chunkSize: ${nextChunk.size}")
            nextPageIndex += program_size
            nextChunk
        } catch (e: Exception) {
            null
        }
    }

}