target_sources(
  app
  PRIVATE main.c
          cdr.c
)

if(NOT
   CONFIG_MEMFAULT_RAM_BACKED_COREDUMP
)
  target_sources(app PRIVATE memfault_platform_coredump_storage.c)
endif()
