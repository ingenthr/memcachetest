require 'rubygems'
require 'sinatra'
require 'fileutils'

MEMCACHETEST = ARGV[0] || "./memcachetest"

NAME = "memcachetestWithFries"

print "#{NAME}\n"
print "MEMCACHETEST is #{MEMCACHETEST}\n"

def header
<<EOH
  <html>
  <head>
    <style>
      * { font-size: 9pt; }
      body { font-family: verdana; }
      .ex, .ex li {font-size: 7pt;}
      .footer, .footer a { font-size: 7pt; }
    </style>
  </head>
  <body>
    <h1><a href="/">#{NAME}</a></h1>
    <hr/>
EOH
end

def footer
<<EOH
  <hr/>
  <div class="footer">
    <a href="/">#{NAME}</a>
       is a little bit of yummy, greasy web-UI around the main meal of
       <a href="http://github.com/ingenthr/memcachetest">memcachetest</a>
  </div>
  </body>
  </html>
EOH
end

# --------------------------------------------------------

get '/' do
  files = Dir.glob("./tmp/#{NAME}_result_*")
  links = files.sort.reverse.map do |f|
     x = f.gsub("./tmp/", "").gsub("_", "/")
     "<a href=\"#{x}\">#{x}</a>"
  end
  links = ["no previous results"] if links.empty?

  body = <<EOH
  <table border="0" width="100%" cellpadding="10">
  <tr>
    <td valign="top">
    <h2>start memcachetest...</h2>

    <form action="/#{NAME}" method="post">
      <ul>
        <li>
          <label>arguments:</label>
          <input type="text" name="c" value="-h 127.0.0.1:11211 -v" size="60"/>
          <div class="ex">
            examples:
            <ul>
              <li>-h 127.0.0.1:11211 -v
              </li>
              <li>-h 127.0.0.1:11211
              </li>
            </ul>
          </div>
        </li>
      </ul>
      <input type="submit" name="go" value="start"/>
    </form>

    <hr/>
    <h2>previous results...</h2>
    <ul>
      <li>#{links.join('</li><li>')}</li>
    </ul>

    <hr/>
    <h2>misc...</h2>
    <form action="/#{NAME}/clear_results" method="post">
      <input type="submit" name="go" value="clear all results"/>
    </form>
    </td>

    <td valign="top">
<pre><code>
$ ./memcachetest --help
Usage: memcachedtest [-h host[:port]] [-t #threads] [-T] [-i #items]
            [-c #iterations] [-v] [-V] [-f dir] [-s seed]
            [-W size] [-x] [-y stddev] [-k keyfile]
	-h The hostname:port where the memcached server is running
	-t The number of threads to use
	-m The max message size to operate on
	-F Use fixed message size
	-i The number of items to operate with
	-c The number of iteratons each thread should do
	-l Loop and repeat the test, but print out information for each run
	-V Verify the retrieved data
	-v Verbose output
	-L Use the specified memcached client library
	-M Use multiple servers (specified with -h)
	-W connection pool size
	-s Use the specified seed to initialize the random generator
	-S Skip the populate of the data
	-P The probability for a set operation
	-y Specify standard deviation for -x option test
	-k The file with keys to be retrieved
	-x randomly request from a set in a supplied file
		(implies -S, requires -k)
</code></pre>
    </td>
  </tr>
  </table>
EOH

  header + body + footer
end

post "/#{NAME}" do
  c = params[:c]
  if c and (not c.match(/[\/;<>&]/)) and (not c.match(/\.\./))
    FileUtils.mkdir_p('./tmp')
    id = Time.now.strftime("%Y%m%d-%H%M%S")
    out = "./tmp/#{NAME}_result_#{id}"
    cmd = system("#{MEMCACHETEST} #{c} &>#{out} &")
    body = <<EOH
      starting memcachetest with params: #{c}
      <br/>
      <br/>
      results will be available at: <a href="/#{NAME}/result/#{id}">/#{NAME}/result/#{id}</a>
EOH
  else
    body = "whoops, wrong arguments"
  end

  header + body + footer
end

get "/#{NAME}/result/:x" do
  id = params[:x]
  if id and id.match(/^[0-9\-]+$/)
    out = "./tmp/#{NAME}_result_#{id}"
    if File.exists?(out)
      link = "<a href=\"/#{NAME}/result/#{id}\">/#{NAME}/result/#{id}</a>"
      if File.size(out) > 0
        res = (File.open(out, 'r') {|f| f.read })
        body = <<EOH
          results for #{link}:
          <br/>
          <pre><code>
#{res}
          </code></pre>
EOH
      else
        body = <<EOH
          currently no results for #{link}.
          <br/>
          <br/>
          please be patient and check at #{link} soon
EOH
      end
    end

    body = "unknown #{NAME} job: #{id}" unless body
  else
    body = "not a valid #{NAME} job or result"
  end
  header + body + footer
end

post "/#{NAME}/clear_results" do
  files = Dir.glob("./tmp/#{NAME}_result_*")
  FileUtils.rm_f(files)
  body = "removed result files:<br/><br/>#{files.join('<br/>')}"
  header + body + footer
end
