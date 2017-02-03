all: select poll

select: select.c
	arm-cortex_a8-linux-gnueabi-gcc select.c -o select
poll: poll.c
	arm-cortex_a8-linux-gnueabi-gcc poll.c -o poll
clean:
	rm select poll
