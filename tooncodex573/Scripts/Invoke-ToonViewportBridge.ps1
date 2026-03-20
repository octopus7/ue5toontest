param(
    [ValidateSet("ping", "capture_viewport")]
    [string]$Command = "ping",

    [string]$Address = "127.0.0.1",

    [int]$Port = 6767,

    [string]$Path = "",

    [int]$ResX = 0,

    [int]$ResY = 0,

    [int]$BookmarkIndex = -1
)

$payload = [ordered]@{
    command = $Command
}

if ($Command -eq "capture_viewport") {
    if ($Path) {
        $payload.path = $Path
    }

    if (($ResX -gt 0) -or ($ResY -gt 0)) {
        if (($ResX -le 0) -or ($ResY -le 0)) {
            throw "ResX and ResY must both be positive when provided."
        }

        $payload.res_x = $ResX
        $payload.res_y = $ResY
    }

    if (($Command -eq "capture_viewport") -and ($BookmarkIndex -ge 0)) {
        $payload.bookmark_index = $BookmarkIndex
    }
}

$json = $payload | ConvertTo-Json -Compress
$response = $null

$client = [System.Net.Sockets.TcpClient]::new($Address, $Port)
try {
    $stream = $client.GetStream()
    $encoding = [System.Text.UTF8Encoding]::new($false)
    $writer = [System.IO.StreamWriter]::new($stream, $encoding, 1024, $true)
    try {
        $writer.NewLine = "`n"
        $writer.WriteLine($json)
        $writer.Flush()

        $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $false, 1024, $true)
        try {
            $response = $reader.ReadLine()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $writer.Dispose()
    }
}
finally {
    $client.Dispose()
}

if ([string]::IsNullOrWhiteSpace($response)) {
    throw "No response from viewport bridge."
}

$response
