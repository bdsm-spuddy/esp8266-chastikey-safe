static const String change_ap_html = R"EOF(
<html>
<body>

<form method=post enctype="multipart/form-data">
To set the WiFi network details, enter here:
<br>
WiFi SSID: <input name=ssid size=40>
<br>
WiFi Password: <input name=password size=40>
<br>
<input type=submit value="Set WiFi" name=setwifi>
<hr>
If the change is accepted, the safe will reboot after 5 seconds.
</form>
</body>
</html>
)EOF";

static const String change_auth_html = R"EOF(
<html>
<body>
<form method=post action=safe/ enctype="multipart/form-data">
To set the user name and password needed to access the safe:
<br>
Safe Username: <input name=username size=40>
<br>
Safe Password: <input name=password size=40>
<p>
<input type=submit value="Set Auth Details" name=setauth>
<p>
If the change is accepted, you will need to login again.
<hr>
To talk to the Chastikey Server you need to enter your API
details.  These can be found/generated from the App.
<br>
API Key: <input name=apikey size=40 value="##apikey##">
<br>
API Secret: <input name=apisecret size=40 value="##apisecret##">
<p>
<input type=submit value="Set API Details" name=setapi>
<p>
If the values are incorrect you will not be able to talk to the server
and will need to correct the values.
<hr>
You need to enter the username you set in the application <i>or</i> your
DiscordID if you have linked Discord to the app
<br>
If you change this in the middle of a lock then you might lose access
to that lock, so beware!<br>
<select name=idtype>
<option value="u" ##usernameselected##>Username
<option value="d" ##discordselected##>Discord ID
</select>
<input name=idvalue size=40 value="##idvalue##">
<p>
<input type=submit value="Set User Details" name=setuser>
<p>
The default endpoint this software talks to is the Chastikey v0.5 API.
This may be changed here (if no lock is running) to another endpoint
that supports the <tt>checklock.php</tt> endpoint.  NOTE: the server
must also use a LetsEncrypt certificate so we can do secure communication.
<p>
<input name=apiurl size=50 value="##apiurl##">
<p>
<input type=submit value="Change API URL" name=setapiurl>
</form>
</body>
</html>
)EOF";

static const String index_html = R"EOF(
<!DOCTYPE html>
<html>

<head>
<title>Safe</title>
<link rel="shortcut icon" href="data:image/x-icon;base64,AAABAAEAEBAQAAEAAwAoAQAAFgAAACgAAAAQAAAAIAAAAAEABAAAAAAAAAAAAAAAAAAAAAAAEAAAABAAAAAAAAAAgAAAAACAAACAgAAAAACAAIAAgAAAgIAAwMDAAICAgAD/AAAAAP8AAP//AAAAAP8A/wD/AAD//wD///8A///////////////u7u//////7u7u7u////7gAAAA7v//7u7gAAAO7//uAA4A8ADv/uAAAOAAAO7+4AAA4AAO7v7gDwDuAODu/uAAAODuAO7+4AAOAAAA7v/uAOAA8A7v/+7g4AAA7u///u4OAA7u////7u7u7u//////7u7v//8AAP//AAD//wAA//8AAP//AAD//wAA//8AAP//AAD//wAA//8AAP//AAD//wAA//8AAP//AAD//wAA//8AAP//" />
</head>

<frameset rows = "8%,*">
  <frame name = "top" src = "top_frame.html" />

  <frameset cols = "30%,70%">
    <frame name = "menu" src = "menu_frame.html" />
    <frame name = "main" src = "safe/?status=1" />
  </frameset>

  <noframes>
     <body>Your browser does not support frames.</body>
  </noframes>

</frameset>

</html>
)EOF";

static const String main_frame_html = R"EOF(
main
)EOF";

static const String menu_frame_html = R"EOF(
<html>
<head>
  <base target="main">
  <title>Safe menu</title>
</head>
<body>
<center><h2>Menu</h2>
<p>
<form method=post action=safe/ enctype="multipart/form-data">
<input type=submit value=Status name=status>
</form>
<hr>
<form method=post action=safe/ enctype="multipart/form-data">
Open safe door:<br>
<select name="duration">
  <option value="5">5 seconds</option>
  <option value="10">10 seconds</option>
  <option value="20">20 seconds</option>
  <option value="30">30 seconds</option>
  <option value="60">60 seconds</option>
</select>&nbsp;&nbsp;
<input type=submit value="Open Safe" name=open>
</form>
<hr>
<form method=post action=safe/ enctype="multipart/form-data">
Set Lock Session<br>
This is the lock <b>GROUP</b> ID value:<br>
<input type=input name=session length=40><br>
<input type=submit value="Set Lock" name=set_lock>
</form>
<hr>
<a href="change_auth.html">Change Safe Authentication Details</a>
</center>
</form>
</body>
</html>
)EOF";

static const String top_frame_html = R"EOF(
<html>
<body>
<center><h1>Chastikey safe controls</h1></center>
</body>
</html>
)EOF";

