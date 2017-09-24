# debian-devicedriver
This code is about to send the signal to the processes using the Armadillo(embedded CPU board).
There are 3 buttons on the Armadillo, and using these buttons I did 2 things.
First one is to distinguish which butons are pressed. Just add the differnt number when each buttons are prssed such as the "rightest button → +1","middle button → +2","left button → +4".As you do this if the summerised number is 3, it means right button and the middle buttons are pressed.
Second thing I did is making some functions something like a timer.If you press the right button it starts counting the number in eac seconds. After that, if you press the middle button it stops the timer and clear it automatically. Sending the different signals(right button → SIGUSR1,middle buton → SIGUSR2) makes it possibe.
