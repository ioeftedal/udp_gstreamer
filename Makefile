b1:
	GST_DEBUG=*:WARN gcc basic-tutorial-1.c -o basic-tutorial-1 `pkg-config --cflags --libs gstreamer-1.0`

clean:
	rm -rf basic-tutorial-1
