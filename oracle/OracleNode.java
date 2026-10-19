package oracle;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.StandardSocketOptions;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.file.Path;
import java.util.Iterator;

public class OracleNode {
    
    static class NodeState{
        ByteBuffer buf = ByteBuffer.allocate(Protocol.RECV_BUF_SIZE);
        int index;
        byte[] ip;
        int port;
        boolean connected;
    } // stores info about a particular VN connected to the oracle

    static int nextIndex = 0;

    public static void main(String[] args) throws Exception {
        if (args.length != 1){
            System.err.println("usage: java -cp build oracle.OracleNode <config-file>");
            System.exit(1);
        }
        int[][] cost = null;
        try{
            cost = Config.parse(Path.of(args[0]));
        } catch (Config.ConfigException e){
            System.err.println("Bad Config: " + e.getMessage());
            System.exit(1);
        }
        System.out.println("Config OK " + cost.length + " Nodes");
        Selector selector = Selector.open();
        ServerSocketChannel server = ServerSocketChannel.open();
        server.setOption(StandardSocketOptions.SO_REUSEADDR, true);
        server.bind(new InetSocketAddress(Protocol.ON_PORT));
        server.configureBlocking(false);
        server.register(selector,SelectionKey.OP_ACCEPT);
        System.out.println("ON listening on port "  +Protocol.ON_PORT);

        while(true){
            selector.select(1000); // 1 second timeout
            Iterator<SelectionKey> it = selector.selectedKeys().iterator();
            while(it.hasNext()){
                SelectionKey key = it.next();
                it.remove();

                if (!key.isValid()) continue;
                try{
                    if (key.isAcceptable()){
                        handleAccept(server,selector);
                    }
                    else if (key.isReadable()){
                        handleRead(key);
                    }
                }
                catch (IOException e){
                    System.out.println("connection error: " + e.getMessage());
                    if (key.channel() != server) {
                        close(key);
                    }
                }
            }
        }

    }

    static void handleAccept(ServerSocketChannel server, Selector selector) throws IOException{
        SocketChannel ch = server.accept();
        if (ch == null) return;
        if (nextIndex >= Protocol.MAX_NODES){
            System.out.println("too many connections");
            ch.close(); return;
        }

        ch.configureBlocking(false);
        NodeState st = new NodeState();
        st.index = nextIndex++;
        ch.register(selector, SelectionKey.OP_READ, st);
        System.out.println("accepted connection " + (char)('A' + st.index) + " from " + ch.getRemoteAddress());
    }

    static void handleRead(SelectionKey key) throws IOException{
        SocketChannel ch = (SocketChannel) key.channel();
        NodeState st = (NodeState) key.attachment();

        int n = ch.read(st.buf);
        if (n == -1){
            System.out.println((char)('A' + st.index) + " disconnected");
            close(key);
            return;
        }

        st.buf.flip();

        while (st.buf.remaining() >= Protocol.HEADER_LEN) {
            int type = Byte.toUnsignedInt(st.buf.get(st.buf.position()));
            int bodyLen = Short.toUnsignedInt(st.buf.getShort(st.buf.position() + 1));

            if (type != Protocol.TYPE_CONNECT || bodyLen != Protocol.CONNECT_BODY_LEN) {
                System.out.println("protocol error: type=" + type + " len=" + bodyLen);
                close(key);
                return;
            }
            
            if (st.buf.remaining() < Protocol.HEADER_LEN + bodyLen) {
                break;
            }

            st.buf.position(st.buf.position() + Protocol.HEADER_LEN);   
            boolean ok = handleFrame(st, type, bodyLen);
            if (!ok) {   
                close(key);               
                return;
            }
        }
        st.buf.compact();
    }

    static boolean handleFrame(NodeState st, int type, int bodyLen) {
        if (type != Protocol.TYPE_CONNECT || bodyLen != Protocol.CONNECT_BODY_LEN) {
            System.out.println("protocol error: type=" + type + " len=" + bodyLen);
            return false;
        }

        byte[] ip = new byte[4];
        st.buf.get(ip);                                      
        int port = Short.toUnsignedInt(st.buf.getShort());  

        st.ip = ip;
        st.port = port;
        st.connected = true;

        try {
            System.out.println((char) ('A' + st.index) + " = "
                    + InetAddress.getByAddress(ip).getHostAddress() + ":" + port);
        } catch (IOException e) {
           
        }
        return true;
    }

     static void close(SelectionKey key) {
        try { key.channel().close(); } catch (IOException ignored) {}
        key.cancel();
     
    }

}
