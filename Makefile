CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99
TARGET = filewatch

# Should also have configure file

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c

#doc:
	#Todo
	#Doxygen

# Delete temporary files
clean:
	$(RM) $(TARGET)

# Clean but also binaries and libs
#distclean:
#	$(RM) $(TARGET)

#check:
#	echo 'tests'

