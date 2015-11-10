var fs = require("fs");
var uglifyjs = require("uglify-js");
var uglifycss = require('uglifycss');
var gzipme = require('gzipme');

var uglyjs = uglifyjs.minify(["../js/jquery-2.1.3.min.js", "../js/jquery.cookie.min.js", "../js/jquery.flot.min.js",
                            "../js/jquery.flot.resize.min.js", "../js/jquery.flot.navigate.min.js",  "../js/bootstrap.min.js",
                            "../js/bootstrap-slider.min.js", "../js/interface.js"]);

fs.writeFile("ugly.min.js", uglyjs.code, function(err) {
    if (err) {
        return console.log(err);
    }
    console.log("JS was minimized!");
});

var uglycss = uglifycss.processFiles(
    [ '../css/bootstrap-slider.min.css', '../css/bootstrap-theme.min.css', '../css/bootstrap.min.css',
        '../css/defaults.css'],
        { maxLineLen: 500, expandVars: true }
);

fs.writeFile("ugly.min.css", uglycss, function(err) {
    if (err) {
        return console.log(err);
    }
    console.log("CSS was minimized!");
});

gzipme("ugly.min.css", "../ugly.min.css.gz");
gzipme("ugly.min.js", "../ugly.min.js.gz");

fs.unlinkSync("ugly.min.css");
fs.unlinkSync("ugly.min.js");
console.log("Files compressed!");
