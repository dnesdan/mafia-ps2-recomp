execute_process(COMMAND "${EXECUTABLE}" --unresponsive-unwind
    TIMEOUT 8 RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 124 OR NOT error MATCHES "EE shutdown timed out during unwinding")
    message(FATAL_ERROR "Unresponsive synthetic executor did not exit124 with diagnostics: ${status}\n${output}\n${error}")
endif()
