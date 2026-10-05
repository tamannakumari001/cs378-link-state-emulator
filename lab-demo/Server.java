import java.net.*;
import java.io.*;

public class Server {
    public static void main(String[] args){
        try
        {
            ServerSocket serverSocket = new ServerSocket(5000);

            System.out.println("Server started on port 5000. Waiting for client...");

            Socket socket = serverSocket.accept();
            System.out.println("Client accepted yayy");

            DataInputStream in = new DataInputStream(new BufferedInputStream(socket.getInputStream()));

            String msg = "";

            while(true){
                byte[] buffer = new byte[1024];

                int n = in.read(buffer);

                if (n == -1)
                    break;

                String tmp = new String(buffer, 0, n);

                System.out.println("Read word: " + tmp);
                msg += " " + tmp;

                if (tmp.equals("BYE"))
                    break;
            }
            System.out.println("Final sentence: " + msg);
            socket.close();
            serverSocket.close();
        }
        catch(IOException i)
        {
            System.out.println(i);
        }
    }
}
