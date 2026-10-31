<?php

header('Content-Type: application/json; charset=utf-8');

function response(
    $success,
    $message,
    $data = null,
    $status = 200
) {

    http_response_code($status);

    $response = [
        'success' => $success,
        'message' => $message
    ];

    if ($data !== null) {
        $response['data'] = $data;
    }

    echo json_encode(
        $response,
        JSON_UNESCAPED_UNICODE
    );

    exit;
}