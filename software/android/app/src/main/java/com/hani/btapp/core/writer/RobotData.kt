package com.hani.btapp.core.writer

import com.hani.btapp.utils.toBytes

/**
 * Created by hanif on 2022-08-07.
 */
class RobotData(private val data: ByteArray) {
    private val robot_data = data.size.toBytes(2) + data
    private val size = robot_data.size
    private var bytesProcessed = 0
    private var tx_counter = 0

    private var defaultPacketSizeBytes = 240
    var remainingBytes = size
       private set

    fun getNextChunk(): ByteArray? {
        var bytesToProcess = defaultPacketSizeBytes
        remainingBytes = size - bytesProcessed
        if (remainingBytes <= 0) {
            return null
        }
        if (remainingBytes < defaultPacketSizeBytes) {
            bytesToProcess = remainingBytes
        }
        val nextPacket = robot_data.copyOfRange(
            fromIndex = bytesProcessed,
            toIndex = bytesProcessed + bytesToProcess
        )
        bytesProcessed += bytesToProcess
        return nextPacket
    }

    fun reset() {
        remainingBytes = size
        tx_counter = 0
    }

}