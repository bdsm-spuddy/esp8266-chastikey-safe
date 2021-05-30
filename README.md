## Chastikey Safe software

This is a version of the software for my [digital safe](https://bdsm.spuddy.org/writings/Safe_v2/) designed to work uniquely with [Chastikey](https://chastikey.com).

Follow the instructions on that page to build the hardware, but the software
here can be uploaded to the ESP8266 board instead.

Instead of entering a combination, you can enter a lockGroupID value
instead.


### Diffences between the normal software.

On the original software you just needed to enter a username/password
to protect the safe from having an outsider change things.

In this version there are a few more things to configure:

* API Key  and  API Secret

These are set up by the mobile application from the "API" menu.  These
are needed to talk to the Chastikey API

* Username or Discord ID

If you select the "username" option then you need to make sure you enter
the same value as you have defined in the application.  Now this may not
work too well with some UTF8 characters in the username.  If you change
your name in the app then you'll need to change your name in the safe.

If you select the "discordID" option then you need to have be on the
Discord server (invite available from the chastikey website) and use
the "kiera" function to link your application to discord.

Fun option... you don't _need_ to use your own data; if you want to
be locked under another's lock ("I'm locked for as long as _otherperson_
is locked") then you could enter their details here!  But beware... if
they delete the lock from the app, this safe will never open...

### Typical usage

* Setup the username/password, apikey/secret and username/discordID.

You can test this all works by then tryin a dummy lock; enter `123` as
the lock ID.  If it all works then you should get the response
`No lock found! Either the lockID is wrong or you deleted it...`.  Otherwise
you'll get an error explaining what is wrong

* Create the lock in the app

Create the lock as normal, but you don't need to look at the combination;
just go through the next screen.  You'll still get the random numbers to
help you forget, but if you didn't even look at the combination then
you'll not need to forget it!

* Click on the "..." option next to the newly created lock.  The value
you need is the _lock Group ID_ value.  You can now enter this into
the main menu and click on "Set Lock".  Your safe is now locked until
the app unlocks it!

### Fake locks

If you create fake locks then each lock will be displayed on the "Status"
screen.  If one of them is unlocked then you'll either see "UnlockedFake" or
"UnlockedReal".  The safe will stay closed until you get the "UnlockedReal"
result.

e.g after creating a lock with 2 fakes.

    There are 3 locks
    Lock 1: Locked
    Lock 2: Locked
    Lock 3: Locked

After getting the combination for a fake lock

    There are 3 locks
    Lock 1: Locked
    Lock 2: UnlockedFake
    Lock 3: Locked

After getting the real lock:

    There are 3 locks
    Lock 1: UnlockedFake
    Lock 2: UnlockedFake
    Lock 3: UnlockedReal

### Refresh status

Every time you reload the web page or click the "Status" button, the
safe will reconnect to the API and check the lock status.  This isn't
always fast because the ESP8266 isn't a fast CPU.  It can take between
2 and 10 seconds.  This is because we use TLS to prevent "man in the
middle" attacks.  After all, we don't want to be able to unlock the
safe early!

If there are communication issues then these will be displayed.

*IMPORTANT* if you lose internet access or the server goes down or
there's some other problem then you can't refresh the status!
ALWAYS ensure you have a backup key...

### Lock completion

Once a lock has reached "UnlockedReal" state then the safe will delete
the lockID, which will then let you open it.

    There are 3 locks
    Lock 1: UnlockedFake
    Lock 3: UnlockedReal
    Lock 2: UnlockedFake

    This lock is now available to unlock, and has been removed from the safe


