package com.hani.btapp.core.service

import android.content.Context
import com.hani.btapp.Logger
import com.hani.btapp.core.com.CommunicationScope
import com.hani.btapp.core.com.ServiceCommunicationChannel
import com.hani.btapp.core.writer.*
import com.hani.btapp.utils.toBytes
import com.hani.btapp.utils.toHexString
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.io.File
import java.util.*
import javax.inject.Inject
import javax.inject.Singleton
import java.util.concurrent.Semaphore

/**
 * Created by hanif on 2022-08-21.
 */

private const val RESPONSE_OK = 0
private const val RESPONSE_NOK = -1
private const val MAX_NR_UPDATE_ATTEMPTS = 10
private const val FLASH_START_ADDRESS = 0x2F000
private const val MAGIC_CODE = "BL702BOOT"

@Singleton
class FirmwareUpdateHandler @Inject constructor(
    @ApplicationContext private val context: Context,
    private val scope: CommunicationScope,
    private val serviceCommunicationChannel: ServiceCommunicationChannel,
) : RobotStateListener, FirmwareStepsHandler {

    private lateinit var gattInteractor: GattInteractor
    private lateinit var firmwareData: BleFirmwareData

    // Used to check if response if sent from the robot.
    // This will be filled when onCharacteristicChanged is called.
    private val rxQueue = LinkedList<ByteArray>()

    private var firmwareStepsUiState = FirmwareUpdateUiState()
    private var responseJob: Job? = null
    private var firmwareUpdateJob: Job? = null
    private val tx_done_sem: Semaphore = Semaphore(1, true)

    override fun onDisconnected(userInitiated: Boolean) {
        Logger.log("Robot disconnected: User initiated: $userInitiated")

        if (userInitiated) {
            firmwareUpdateJob?.cancel()
            broadcastFirmwareStepUiState { firmwareStepsUiState = FirmwareUpdateUiState() }
        }

        responseJob?.cancel()
        clearRxQueue()
        if (this::firmwareData.isInitialized) {
            firmwareData.reset()
        }
    }

    override fun onDataWritten() {
        tx_done_sem.release()
    }

    override fun onResponse(data: ByteArray) {
        rxQueue.add(data)
    }

    override fun setGattInteractor(gattInteractor: GattInteractor) {
        this.gattInteractor = gattInteractor
        firmwareUpdateJob?.cancel()
        responseJob?.cancel()
        clearRxQueue()
        if (this::firmwareData.isInitialized) {
            firmwareData.reset()
        }
        clearStateForUi()
    }

    override fun onStartUpdatingFirmware() {
        var isSuccess = false
        var isInBootloader = false
        firmwareUpdateJob?.cancel()
        firmwareUpdateJob = scope.launch {
            // Read in firmware data
            val data = getFirmwareData()
            data?.let { data ->
                firmwareData = BleFirmwareData(data)
                val size = data.size
                Logger.log("Firmware file read: $size bytes")

                //for (attempts in 0..MAX_NR_UPDATE_ATTEMPTS) {
                    //if (firmwareUpdateJob?.isActive == false) {
                        //Logger.log("firmwareUpdateJob canceled")
                    //}
                    //Logger.log("Attempt number: ${attempts + 1}")

                    broadcastFirmwareStepUiState {
                        firmwareStepsUiState = FirmwareUpdateUiState(isUpdating = true)
                    }

                    gattInteractor.connectIfNeeded()

                    isInBootloader = if (!verifyInBootloaderMode()) {
                        enterBootLoaderMode()
                    } else {
                        true
                    }

                    if (isInBootloader) {
                        if (verifyInBootloaderMode()) {
                            gattInteractor.clearConnnectionStatus()
                            gattInteractor.enableReconnect()

                            if (eraseFlash(size)) {
                                var retry = 4

                                while (retry > 0) {
                                    if (!gattInteractor.waitNewConnection()) {
                                        if (verifyInBootloaderMode()) {
                                            break
                                        }
                                    } else {
                                        break
                                    }
                                    retry = retry - 1
                                }

                                if (verifyInBootloaderMode()) {
                                    updateFirmwareUiStep(FirmwareUpdateUiStep.RECONNECTING)
                                    updateFirmwareUiStep(FirmwareUpdateUiStep.SENDING_FIRMWARE)
                                    firmwareData.reset()

                                    var firmwareChunk = firmwareData.getNextChunk()
                                    var response = RESPONSE_NOK
                                    var prog_addr = FLASH_START_ADDRESS

                                    var program_success_count = 0
                                    while (firmwareChunk != null) {
                                        retry = 10
                                        while (retry > 0) {
                                            response = sendOnePage(prog_addr, firmwareChunk)
                                            if (response == RESPONSE_OK) {
                                                break
                                            } else {
                                                if (!gattInteractor.IsConnected()) {
                                                    var retry_connect = 5
                                                    while (retry_connect > 0) {
                                                        if (!gattInteractor.waitNewConnection()) {
                                                            if (verifyInBootloaderMode()) {
                                                                break
                                                            }
                                                        } else {
                                                            break
                                                        }
                                                        retry_connect = retry_connect - 1
                                                    }

                                                    if (retry_connect == 0) {
                                                        retry = 0
                                                        break
                                                    }
                                                }
                                            }
                                            retry = retry - 1
                                        }

                                        if (retry == 0) {
                                            break
                                        }

                                        val totalBytes = firmwareData.totalBytes
                                        val bytesRead = firmwareData.bytesRead
                                        val bytesReadPercentage = (bytesRead.toDouble() / totalBytes.toDouble()) * 100
                                        Logger.log("$bytesRead/$totalBytes : $bytesReadPercentage %")
                                        broadcastFirmwareStepUiState {
                                            firmwareStepsUiState = firmwareStepsUiState.copy(
                                                totalBytes = totalBytes,
                                                sentBytes = bytesRead
                                            )
                                        }

                                        prog_addr = prog_addr + firmwareChunk.size
                                        firmwareChunk = firmwareData.getNextChunk()
                                    }

                                    if (firmwareChunk == null) {
                                        Logger.log("Firmware sent")
                                        updateFirmwareUiStep(FirmwareUpdateUiStep.FIRMWARE_SENT)
                                        systemReset()
                                        isSuccess = true
                                    }
                                }
                            }
                        }
                    }

                    gattInteractor.disableReconnect()

                    if (isSuccess) {
                        updateFirmwareUiStep(FirmwareUpdateUiStep.COMPLETE)
                        //break
                    } else {
                        updateFirmwareUiStep(FirmwareUpdateUiStep.FAILURE)
                        gattInteractor.disconnectDevice()
                    }
                //}

                broadcastFirmwareStepUiState {
                    firmwareStepsUiState = firmwareStepsUiState.copy(isUpdating = false)
                }
            }
        }
    }

    private fun broadcastFirmwareStepUiState(update: () -> Unit) {
        update.invoke()
        serviceCommunicationChannel.publishFirmwareStepState(firmwareStepsUiState)
    }

    private fun getFirmwareData(): ByteArray? {
        val file = File(context.filesDir, "firmwarewbootheader.bin")
        if (!file.exists()) {
            Logger.log("Internal firmware file could not be found")
            return null
        }
        return file.readBytes()
    }

    private fun updateFirmwareUiStep(step: FirmwareUpdateUiStep) {
        broadcastFirmwareStepUiState {
            firmwareStepsUiState = firmwareStepsUiState.addStep(step)
        }
    }

    private fun clearStateForUi() {
        broadcastFirmwareStepUiState {
            firmwareStepsUiState = FirmwareUpdateUiState()
        }
    }

    private fun clearRxQueue() {
        rxQueue.clear()
    }

    private suspend fun enterBootLoaderMode() : Boolean {
        Logger.log("Entering boot loader")
        updateFirmwareUiStep(FirmwareUpdateUiStep.ENTERING_BOOTLOADER)

        var is_write_success = writeRawData(MAGIC_CODE.encodeToByteArray())

        if (is_write_success) {
            gattInteractor.clearConnnectionStatus()
            delay(2000)
            gattInteractor.enableReconnect()
            gattInteractor.makeNewConnection()
            Logger.log("Robot in Boot loader mode")
        }
        return is_write_success
    }

    private suspend fun eraseFlash(firmwareSize: Int) : Boolean {
        Logger.log("Erasing flash")
        updateFirmwareUiStep(FirmwareUpdateUiStep.ERASING_FLASH)
        clearRxQueue()
        val eraseFlashPayload = EraseFlashCommandPayload(firmwareSize)
        val datapkg = RobotData(eraseFlashPayload.get())
        val bytetosend = datapkg.getNextChunk()
        var is_write_success = false
        bytetosend?.let { is_write_success = writeRawData(it) }
        Logger.log("Flash erased")

        return is_write_success
    }

    private suspend fun verifyInBootloaderMode(): Boolean {
        clearRxQueue()
        val readJedecPayload = ReadJedecCommandPayload()
        val datapkg = RobotData(readJedecPayload.get())
        var is_write_success = false
        datapkg.getNextChunk()?.let { is_write_success = writeRawData(it) }

        if (is_write_success) {
            var timeout = 1000
            Logger.log("Waiting for response...")
            while (rxQueue.isEmpty() && timeout > 0) {
                timeout -= 1
                delay(1)
            }

            if (timeout == 0) {
                return false
            }
        } else {
            return false
        }

        return true
    }

    private suspend fun sendOnePage(flashaddr: Int, pageData: ByteArray): Int {
        clearRxQueue()
        val onePagePayload = ProgramOnePageCommandPayload(flashaddr, pageData).get()
        Logger.log("Transferring firmware: ${onePagePayload.size} bytes")
        val datapkg = RobotData(onePagePayload)
        datapkg.reset()
        var programdata = datapkg.getNextChunk()
        while (programdata != null) {
            if (!rxQueue.isEmpty()) {
                Logger.log("Writing failed, retry")
                val response = rxQueue.pop()
                val responseHex = response.toHexString()
                Logger.log("Received a response: $responseHex, ${String(response)}")
                return RESPONSE_NOK
            }
            var is_write_success = writeRawData(programdata)

            if (is_write_success == false) {
                return RESPONSE_NOK
            }
            programdata = datapkg.getNextChunk()
        }

        return awaitResponse()
    }

    private suspend fun systemReset() {
        Logger.log("Perform system reset")
        updateFirmwareUiStep(FirmwareUpdateUiStep.RESTARTING_SYSTEM)
        val systemResetPayload = SystemResetCommandPayload()
        val datapkg = RobotData(systemResetPayload.get())
        datapkg.getNextChunk()?.let { writeRawData(it) }
        Logger.log("System reset done")
    }

    private suspend fun awaitResponse(): Int {
        var response = RESPONSE_NOK
        try {
            responseJob?.cancel()
            responseJob = scope.launch { response = getResponse() }
            responseJob?.join()
        } catch (c: CancellationException) {
            return response
        }
        return response
    }

    private suspend fun getResponse(): Int {
        var timeout = 4000
        Logger.log("Waiting for response...")
        while (rxQueue.isEmpty() && timeout > 0) {
            timeout -= 1
            delay(1)
        }
        if (timeout == 0) {
            Logger.log("Time out. Did not receive response from BLE device")
            return RESPONSE_NOK
        }
        val response = rxQueue.pop()
        val responseHex = response.toHexString()
        Logger.log("Received a response: $responseHex, ${String(response)}")

        if (response.size != 2) {
            Logger.log("Error: didn't receive enough bytes in the response: ${response.size}")
            return RESPONSE_NOK
        }

        val header = response[0].toUByte().toInt()
        if (response[0].toInt() != 0x4f) {
            Logger.log("Error: response NACK or unknown response: $header")
            return RESPONSE_NOK
        }

        Logger.log("Got valid response: $responseHex: ${String(response)}")
        return RESPONSE_OK
    }

    private fun writeRawData(data: ByteArray) : Boolean {
        tx_done_sem.tryAcquire()
        var is_write_success = gattInteractor.writeData(data)
        if (is_write_success) {
            tx_done_sem.acquire()
        }
        return is_write_success
    }


}