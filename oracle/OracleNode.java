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
import java.nio.file.Files;
import java.nio.file.attribute.FileTime;
import java.util.Arrays;

public class OracleNode {
    
    static class NodeState{
        ByteBuffer buf = ByteBuffer.allocate(Protocol.RECV_BUF_SIZE);
        int index;
        byte[] ip;
        int port;
        boolean connected;
        SocketChannel ch;
    } // stores info about a particular VN connected to the oracle

    static int nextIndex = 0;
    static int N;
    static int[][] cost;
    static NodeState[] nodes = new NodeState[Protocol.MAX_NODES];
    static boolean BroadcastDone = false;
    static Path configPath;
    static FileTime lastMod;
    static long lastSize;

    static void checkBroadcast(){
        for (int i =0  ; i < N ; i++) if (nodes[i] == null || !nodes[i].connected) return;
        BroadcastDone = true;
        System.out.println ("all " + N + " nodes connected. Sending LINK STATE ... ");
        for (int i = 0; i< Protocol.MAX_NODES;i++){
            if (nodes[i] != null && nodes[i].connected) sendLinkState(nodes[i]);
        }
        
    }

    static void checkConfig() {
        try {
            FileTime mod = Files.getLastModifiedTime(configPath);
            long size = Files.size(configPath);
            if (mod.equals(lastMod) && size == lastSize) return;
            lastMod = mod;
            lastSize = size;

            int[][] newCost = Config.parse(configPath);
            if (newCost.length != N) {
                System.out.println("config changed the number of nodes (" + newCost.length + " != " + N + "). ignoring change");
                return;
            }

            if (Arrays.deepEquals(newCost, cost)) return; // saved without changing anything

            for (int i = 0; i < N; i++) {
                for (int j = i; j < N; j++) {
                    if (newCost[i][j] != cost[i][j]) {
                        System.out.println("link " + (char)('A' + i) + "-" + (char)('A' + j) + ": " + cost[i][j] + " -> " + newCost[i][j]);
                    }
                }
            }

            cost = newCost;
            BroadcastDone = false;
            checkBroadcast();
            if (!BroadcastDone) {
                System.out.println("not all nodes connected. connect all VNs");
            }

        } catch (Config.ConfigException e) {
            System.out.println("Config reload failed: " + e.getMessage());
        } catch (IOException e) {
            System.out.println("could not read config: " + e.getMessage());
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1){
            System.err.println("usage: java -cp build oracle.OracleNode <config-file>");
            System.exit(1);
        }
        configPath = Path.of(args[0]);
        cost = null;
        try{
            cost = Config.parse(configPath);
            lastMod = Files.getLastModifiedTime(configPath);
            lastSize = Files.size(configPath);
        } catch (Config.ConfigException e){
            System.err.println("Bad Config: " + e.getMessage());
            System.exit(1);
        }
        N = cost.length;

        System.out.println("Config OK " + N + " Nodes");
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

            checkConfig();
        }

    }

    static void handleAccept(ServerSocketChannel server, Selector selector) throws IOException{
        SocketChannel ch = server.accept();
        if (ch == null) return;
        if (nextIndex >= Protocol.MAX_NODES){
            System.out.println("too many connections. ignoring island nodes");
            ch.close(); return;
        }

        ch.configureBlocking(false);
        NodeState st = new NodeState();
        st.index = nextIndex++;
        st.ch = ch;
        nodes[st.index] =st;
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
        if (BroadcastDone){
            sendLinkState(st);
        }
        else{
            checkBroadcast();
        }
        return true;
    }

     static void close(SelectionKey key) {
        // forgetting the VN
        Object att = key.attachment();
        if (att instanceof NodeState) {
            NodeState st = (NodeState) att;
            if (nodes[st.index] == st) nodes[st.index] = null;
        }
        try { 
            key.channel().close();
        } catch (IOException ignored) {}
        key.cancel();
     
    }

    

    static void putTuple(ByteBuffer out, NodeState n, int c){
        out.put((byte)('A' + n.index));
        out.put(n.ip);
        out.putShort((short) n.port);
        out.putInt(c);
    }

    static void sendLinkState(NodeState st){
        int i = st.index;
        int k = 1; 
        if (i < N){
            for (int j = 0 ; j < N ;j++){
                if (j != i && cost[i][j] > 0 && nodes[j]!=null && nodes[j].connected)k++;
            }
        }

        int bodyLen = Protocol.LS_COUNT_LEN + Protocol.LS_TUPLE_LEN*k;
        ByteBuffer out = ByteBuffer.allocate(Protocol.HEADER_LEN + bodyLen);
        out.put((byte) Protocol.TYPE_LINK_STATE);
        out.putShort((short) bodyLen);
        out.put((byte) k);
        putTuple(out,st,0);

        if (i < N){
            for (int j = 0 ; j <N;j++){
                if (j != i && cost[i][j] > 0 && nodes[j]!=null && nodes[j].connected){
                    putTuple(out, nodes[j], cost[i][j]);
                }
            }
        }
        out.flip();
        try{
            while (out.hasRemaining()) st.ch.write(out);
            System.out.println("sent LINK-STATE to " + (char)('A' + i) + " and " + k + " tuples");
        } catch (IOException e){
            System.out.println("could not send LINK-STATE to " + (char)('A' + i) + " " + e.getMessage());
        }


        
    }

    

}
