import cv2
import mediapipe as mp
import socket

mp_hands = mp.solutions.hands
mp_drawing = mp.solutions.drawing_utils

cap = cv2.VideoCapture(0)

UDP_IP = "127.0.0.1"
UDP_PORT = 5005
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

with mp_hands.Hands(
    max_num_hands=2,
    min_detection_confidence=0.7,
    min_tracking_confidence=0.7
) as hands:
    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            break

        # Flip the frame for a mirror effect
        frame = cv2.flip(frame, 1)
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = hands.process(rgb_frame)

        if results.multi_hand_landmarks:
            for hand_landmarks in results.multi_hand_landmarks:
                # Index tip (8), Thumb tip (4)
                x = hand_landmarks.landmark[8].x
                y = hand_landmarks.landmark[8].y
                thumb_x = hand_landmarks.landmark[4].x
                thumb_y = hand_landmarks.landmark[4].y

                # Calculate Euclidean distance (normalized)
                pinch_dist = ((x - thumb_x) ** 2 + (y - thumb_y) ** 2) ** 0.5
                is_pinch = 1 if pinch_dist < 0.07 else 0  # Adjust threshold as needed

                win_x = int(x * 1000)
                win_y = int(y * 600)
                msg = f"{win_x},{win_y},{is_pinch}"
                sock.sendto(msg.encode(), (UDP_IP, UDP_PORT))

                mp_drawing.draw_landmarks(
                    frame, hand_landmarks, mp_hands.HAND_CONNECTIONS)

        cv2.imshow('Hand Tracking', frame)
        if cv2.waitKey(1) & 0xFF == 27:  # ESC to quit
            break

cap.release()
cv2.destroyAllWindows()