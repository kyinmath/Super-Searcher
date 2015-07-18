// Place your application-specific JavaScript functions and classes here
// This file is automatically included by javascript_include_tag :defaults

// always blow out of frames
if (window != top) top.location.href = location.href;

// If this page has a login element, put the cursor in it
window.onload = focusForm;
function focusForm() {
    if(document.getElementById("login")){document.getElementById("login").focus();} 
}

// A smidge of js for our bulk upload functionality
var swfuploadSuccess = function (file, server_data, receivedResponse) {
  var progress = new FileProgress(file,  this.customSettings.upload_target);
  progress.setStatus("Complete.");
  progress.toggleCancel(false);
};

// Make the orange line under the final primary navigation element fill all the space
window.onload = fill_out_accent_line;
function fill_out_accent_line() {
    var lis = $('li.nav_1');
    var curr; 
    var prev = lis.first(); 

    if (lis.length == 1) {
	remove_primary_nav();
    } else {
	for (var i = 1; i < lis.length ; i++ ) {
	    curr = $(lis[i]);
	    if (curr.position().left < prev.position().left) {
		insert_first_li(curr);
		insert_last_li(prev);
	    };
	    prev = curr;
	};
	insert_last_li(lis.get(-1));
    };
};

// The orange underline looks dumb if there aren't any links, so nuke it
function remove_primary_nav() {
    $("ul#primary").remove();
};

// insert a 'first' li so we get the correct left margin
function insert_first_li(curr) {
    $(curr).before('<li class="nav_1 first">&nbsp;</li>');
};

// insert a final li on each line to make the orange line go to the edge of the ul
function insert_last_li(last_li) {
    var li = $(last_li);
    var lft_edge = li.position().left - li.parent().position().left;
    var wd = li.parent().width() - (lft_edge + li.width()) - 21;
    if (wd > 0) {li.after('<li class="nav_1 last" style="padding: 0 0 9px 0; width: ' + wd + 'px">&nbsp;</li>')};
};

$(document).ready(function() {
    var original_active = $('li.nav_1.active');
    $('li.nav_1').hover(
	function(){ original_active.removeClass('active') ; $(this).addClass('active') },
	function(){ $(this).removeClass('active') ; original_active.addClass('active')}
    );
});
