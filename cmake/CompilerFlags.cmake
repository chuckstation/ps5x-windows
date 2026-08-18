function(chuckstation5_target_compile_options TARGET_NAME)
  if(MSVC)
    target_compile_options(
      ${TARGET_NAME}
      PRIVATE /W4
              $<$<BOOL:${CHUCKSTATION5_WARNINGS_AS_ERRORS}>:/WX>
              /permissive-
              /Zc:inline
              /Zc:preprocessor
              /Zc:lambda
              /Zc:__cplusplus
              /volatile:iso
              /utf-8
              /D_CRT_SECURE_NO_WARNINGS
              /DNOMINMAX
              /DWIN32_LEAN_AND_MEAN)
    if(CHUCKSTATION5_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_compile_options(${TARGET_NAME} PRIVATE /fsanitize=address)
    endif()
  else()
    target_compile_options(
      ${TARGET_NAME}
      PRIVATE -Wall
              -Wextra
              -Wpedantic
              $<$<BOOL:${CHUCKSTATION5_WARNINGS_AS_ERRORS}>:-Werror>
              -Wno-unused-parameter
              -Wno-missing-field-initializers)
    if(CHUCKSTATION5_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
      target_compile_options(${TARGET_NAME} PRIVATE -fsanitize=address)
      target_link_options(${TARGET_NAME} PRIVATE -fsanitize=address)
    endif()
  endif()
endfunction()
