########################################################################
# Utility blocks that operate on SDR streams
########################################################################
soapy_module(
    TARGET SoapyUtilityBlocks
    SOURCES
        ChannelAligner.cpp
        Converter.cpp
        RandomDropper.cpp
        TxBurstTimer.cpp
        TestConverter.cpp
        TestUtilityBlocks.cpp
    TESTS
        test_converter
        test_channel_aligner
        test_random_dropper
        test_tx_burst_timer
)
