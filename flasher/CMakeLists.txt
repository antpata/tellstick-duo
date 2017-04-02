PROJECT(tellstickduo-flasher)

SET(SRCS
  main.cpp
)

ADD_EXECUTABLE(tellstickduo-flasher
  ${SRCS}
)

FIND_LIBRARY(FTDI_LIBRARY ftdi)

TARGET_LINK_LIBRARIES(tellstickduo-flasher
	${FTDI_LIBRARY}
)

